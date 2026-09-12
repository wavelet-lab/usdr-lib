// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/device.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/eventpoll.h>
#include <linux/delay.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/ktime.h>
#include <linux/pci_hotplug.h>
#include <linux/aer.h>
#include <linux/bitmap.h>
#include <linux/pm_runtime.h>


#include "./pcie_uram_driver_if.h"
#include "./si2c.c"
#include "./spiext.h"
#include "./device_cores.h"

#define DRV_NAME		"usdr"
#define PFX			DRV_NAME ": "

#define DEVICE_NAME		DRV_NAME
#define CLASS_NAME		DRV_NAME

#define XMASS_NAME		"xmass"
#define XPFX			XMASS_NAME ": "
#define XMASS_DEVICE_NAME	XMASS_NAME
#define XMASS_CLASS_NAME	XMASS_NAME

// Maximum device supported for usdr and xmass in the system
#define TOT_USDR_DEVS		32
typedef DECLARE_BITMAP(usdr_dev_bituse_t, TOT_USDR_DEVS);

#define TOT_XMASS_DEVS		8
typedef DECLARE_BITMAP(xmass_dev_bituse_t, TOT_XMASS_DEVS);


MODULE_AUTHOR("Sergey Kostanbaev <contact@wavelet-lab.com>");
MODULE_DESCRIPTION("USDR PCIe UnifiedRAM driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

//#define OLD_IRQ

// Fixups for old kernel modules
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
typedef unsigned __poll_t;
#ifndef EPOLLIN
#define EPOLLIN		0x00000001
#endif

#ifndef EPOLLOUT
#define EPOLLOUT	0x00000004
#endif

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
static int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
    return dev->irq + nr;
}
#endif


// vm_flags_set introduced in 6.3.0, however enterprise-like kernels heavily backport new API to
// old base kernel version, notably RHEL, resulting in newer API in 5.x.x. So version like checks
// will fail here
#ifndef HAVE_VM_FLAGS_SET
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define HAVE_VM_FLAGS_SET 1
#endif
#endif


#ifndef HAVE_CLASS_CREATE_ONE_ARG
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define HAVE_CLASS_CREATE_ONE_ARG 1
#endif
#endif


// Change anytime when extra parameter or meaning is changed in pcie_uram_driver_if.h
#define USDR_DRIVER_ABI_VERSION 3

enum device_flags {
    DEV_VALID = 1,
    DEV_INITIALIZED = 2,
    DEV_EXCLUSIVE = 4,

    DEV_READY_MASK = DEV_VALID | DEV_INITIALIZED,

    // Do not call DMA sync on fully cache coherent systems (extra optimization)
    DEV_NO_DMA_SYNC = 16,
};

struct usdr_dmabuf {
        void* uvirt;        // Mapped to user, need to know VA to properly flush cache (pci_map_single does the trick)
        void* kvirt;
        dma_addr_t phys;    // DMA physical address
};

enum {
    STREAM_USER_CTRL = 1, //User control only
    STREAM_DIR_TX    = 4, //Tx
};

enum {
    VMA_STREAM_IDX_SHIFT = 28,
};

struct stream_core_state_rxbrst {
    unsigned bufptr;
    unsigned bmsk;
};

struct stream_state {
    unsigned dma_buffer_flags;
    unsigned dma_buffs; //Number of allocated dma buffers
    unsigned dma_buff_size; //Size of each buffer in bytes
    unsigned mmap_cfg_offset; //VMA offset to directly mmap buffers to user space

    //unsigned cntr_last;
    //union {
    //    struct stream_core_state_rxbrst rxbrst;
    //} cores;
    u64 abuffer_no;
    u64 bbuffer_no;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
    struct dma_attrs dma_attr;
#endif
    struct usdr_dmabuf dmab[];
};

enum device_interrupts {
    MAX_INT = 32,
    INTNAMES_MAX = 32,
};

typedef irqreturn_t (*irq_func_t)(int irq, void *data);

enum {
    MAX_STREAMS = 32, // up to 16 stream of each direction a device
    MAX_BUCKETS = 4,
};

struct notification_bucket {
    struct usdr_dmabuf db;
    int irq; //Allocated IRQ to check bucket against
    uint32_t rptr;
};

struct usdr_dev;

typedef void (*bucket_func_t)(struct usdr_dev* dev, unsigned event, void* slot);

enum {
	STAT_MAX_SZ = 64,
};

struct event_data_log {
	uint32_t stat_data[4 * STAT_MAX_SZ];
        uint64_t stat_wptr;
        uint64_t stat_rptr;
};

#define MAX_XMASS_DEVS 4
struct xmass_dev {
	struct xmass_dev *next;
	unsigned long device_data;
	struct cdev cdev;
	struct device* cdevice;

	spinlock_t slock;

	unsigned asm_bus_number;
	unsigned devno;
	unsigned dev_mask;

	struct pci_dev* pasmdev; // Valid when device is locked

	struct usdr_dev* dev[MAX_XMASS_DEVS]; // 0 index means device A, 1 - B, ...
};

struct usdr_dev {
        struct usdr_dev *next;
	unsigned long device_data;
	struct cdev cdev;
	struct device* cdevice;
	struct pci_dev* pdev;

	spinlock_t slock;

	void __iomem *bar_addr;

	unsigned devno;
	unsigned dev_mask;

        struct pcie_driver_devlayout dl;

        int irq_configured; // Number of IRQ configured
        irq_func_t irq_funcs[MAX_INT];
        atomic_t irq_ev_cnt[MAX_INT];
        uint32_t rb_ev_data[MAX_INT];
        bucket_func_t bucket_func[MAX_INT];

        wait_queue_head_t irq_ev_wq[MAX_INT];
        char int_names[INTNAMES_MAX * MAX_INT];

        struct stream_state* streams[MAX_STREAMS];

        struct notification_bucket buckets[MAX_BUCKETS];

        unsigned vma_off_last;

        struct event_data_log streaming[MAX_INT];

        struct i2c_cache i2cc[4 * MAX_I2C_COUNT];
        uint32_t i2clut[MAX_I2C_COUNT];
};

// USDR
static struct usdr_dev *usdr_list = NULL;
static struct class* usdr_class = NULL;
static dev_t usdr_dev_first;
static usdr_dev_bituse_t usdr_busy_map;

// XMASS
static struct xmass_dev *xmass_list = NULL;
static struct class* xmass_class = NULL;
static dev_t xmass_dev_first;
static xmass_dev_bituse_t xmass_busy_map;


// #define EXTRA_DEBUG

#ifdef EXTRA_DEBUG
#define DEBUG_DEV_OUT(dev, ...) dev_notice((dev), __VA_ARGS__)
#else
#define DEBUG_DEV_OUT(dev, ...)
#endif

static const
struct pcie_driver_uuid s_uuid[] = {
     { { 0x63, 0x51, 0x02, 0x06, 0x7f, 0x31, 0x44, 0x2a, 0xa7, 0xa1, 0x6c, 0x05, 0xf9, 0xc5, 0xad, 0x48 } },
     { { 0x1f, 0x09, 0xe2, 0x53, 0xc8, 0xad, 0x42, 0xa1, 0x81, 0xab, 0x96, 0x0f, 0x73, 0xeb, 0x3c, 0x62 } },
     { { 0x64, 0xd1, 0x0b, 0x71, 0x9e, 0x68, 0x49, 0x28, 0xa9, 0x6d, 0x56, 0x1b, 0x50, 0x31, 0x06, 0xbf } },
     { { 0xbd, 0x4f, 0xaa, 0x06, 0x78, 0x1b, 0x43, 0x4c, 0xbb, 0xfb, 0xfc, 0xde, 0x77, 0xfd, 0x23, 0xd0 } },
     { { 0x12, 0xc7, 0xdc, 0x11, 0xc4, 0x05, 0x46, 0xd9, 0x83, 0x08, 0x9b, 0xc6, 0x8a, 0xcd, 0x2c, 0x6c } },
     { { 0x04, 0xe5, 0x1d, 0x5c, 0xe6, 0x22, 0x43, 0x74, 0xa4, 0x17, 0x09, 0xf3, 0x53, 0x01, 0xbc, 0xa6 } },
     { { 0x05, 0x8f, 0xa4, 0xa8, 0x75, 0x2a, 0x41, 0x15, 0x9c, 0x09, 0x23, 0x06, 0x8d, 0xc6, 0x8c, 0xdf } },
};


static void usdr_writel(struct usdr_dev *dev, unsigned int off, unsigned int value)
{
    DEBUG_DEV_OUT(&dev->pdev->dev, "REG_WR[%03x] <= %08x\n", off, value);
    iowrite32(cpu_to_be32(value), dev->bar_addr + 4 * off);
}

static unsigned int usdr_readl(struct usdr_dev *dev, unsigned int off)
{
    unsigned int res = be32_to_cpu(ioread32(dev->bar_addr + 4 * off));
    DEBUG_DEV_OUT(&dev->pdev->dev, "REG_RD[%03x] => %08x\n", off, res);
    return res;
}

static void usdr_writeq(struct usdr_dev *dev, unsigned int off, __u64 value)
{
    DEBUG_DEV_OUT(&dev->pdev->dev, "REG_WR[%03x] <= %16llx\n", off, value);
    writeq(cpu_to_be64(value), dev->bar_addr + 4 * off);
}

static __u64 usdr_readq(struct usdr_dev *dev, unsigned int off)
{
    __u64 res = be64_to_cpu(readq(dev->bar_addr + 4 * off));
    DEBUG_DEV_OUT(&dev->pdev->dev, "REG_RD[%03x] => %16llx\n", off, res);
    return res;
}

static void usdr_reg_wr32(struct usdr_dev *dev, unsigned int vaddr, unsigned int value)
{
    unsigned i;
    for (i = 0; i < dev->dl.idx_regsp_cnt; i++) {
        if (vaddr >= dev->dl.idx_regsp_vbase[i]) {
            usdr_writel(dev, dev->dl.idx_regsp_base[i], vaddr - dev->dl.idx_regsp_vbase[i]);
            usdr_writel(dev, dev->dl.idx_regsp_base[i] + 1, value);
            return;
        }
    }

    usdr_writel(dev, vaddr, value);
}

static void usdr_reg_wr64(struct usdr_dev *dev, unsigned int vaddr, __u64 value)
{
    unsigned i;
    for (i = 0; i < dev->dl.idx_regsp_cnt; i++) {
        if (vaddr >= dev->dl.idx_regsp_vbase[i]) {
            // Little endian
            usdr_writel(dev, dev->dl.idx_regsp_base[i], vaddr - dev->dl.idx_regsp_vbase[i]);
            usdr_writel(dev, dev->dl.idx_regsp_base[i] + 1, value >> 32);
            usdr_writel(dev, dev->dl.idx_regsp_base[i], vaddr - dev->dl.idx_regsp_vbase[i] + 1);
            usdr_writel(dev, dev->dl.idx_regsp_base[i] + 1, value);
            return;
        }
    }

    usdr_writeq(dev, vaddr, value);
}

static unsigned int usdr_reg_rd32(struct usdr_dev *dev, unsigned int vaddr)
{
    unsigned i;
    for (i = 0; i < dev->dl.idx_regsp_cnt; i++) {
        if (vaddr >= dev->dl.idx_regsp_vbase[i]) {
            usdr_writel(dev, dev->dl.idx_regsp_base[i], vaddr - dev->dl.idx_regsp_vbase[i]);
            return usdr_readl(dev, dev->dl.idx_regsp_base[i] + 1);
        }
    }

    return usdr_readl(dev, vaddr);
}

static __u64 usdr_reg_rd64(struct usdr_dev *dev, unsigned int vaddr)
{
    unsigned i;
    for (i = 0; i < dev->dl.idx_regsp_cnt; i++) {
        if (vaddr >= dev->dl.idx_regsp_vbase[i]) {
            __u64 ret;
            usdr_writel(dev, dev->dl.idx_regsp_base[i], vaddr - dev->dl.idx_regsp_vbase[i]);
            ret = usdr_readl(dev, dev->dl.idx_regsp_base[i] + 1);
            usdr_writel(dev, dev->dl.idx_regsp_base[i], vaddr - dev->dl.idx_regsp_vbase[i] + 1);
            ret = (ret << 32) | usdr_readl(dev, dev->dl.idx_regsp_base[i] + 1);

            return ret;
        }
    }

    return usdr_readq(dev, vaddr);
}


/*
static irqreturn_t usdr_pcie_irq_spi(int irq, void *data)
{
}


static irqreturn_t usdr_pcie_irq_i2c(int irq, void *data)
{
}
*/

// generic non-specific IRQ
static irqreturn_t usdr_pcie_irq_event(int irq, void *data)
{
    struct usdr_dev *d = (struct usdr_dev *)data;
    int event_no = irq - d->pdev->irq;
    if (event_no < 0 || event_no >= MAX_INT) {
        dev_err(&d->pdev->dev, "Incorrect IRQ Event: %d\n", event_no);
        return IRQ_NONE;
    }


    DEBUG_DEV_OUT(&d->pdev->dev, "IRQ Event: %d; cnt: %d\n", event_no, atomic_read(&d->irq_ev_cnt[event_no]));
    atomic_inc(&d->irq_ev_cnt[event_no]);
    wake_up_interruptible(&d->irq_ev_wq[event_no]);

    return IRQ_HANDLED;
}

#ifdef OLD_IRQ
// MUXed irq events
static irqreturn_t usdr_pcie_irq_muxed(int irq, void *data)
{
    struct usdr_dev *d = (struct usdr_dev *)data;
    unsigned i;

    uint32_t imask = usdr_reg_rd32(d, d->dl.interrupt_base);
    if (imask == 0) {
        return IRQ_NONE;
    }

    for (i = d->irq_configured - 1; i < d->dl.interrupt_count; i++) {
        if ((1u << i) & imask) {
            d->irq_funcs[i](d->pdev->irq + i, data);
        }
    }
    return IRQ_HANDLED;
}
#endif


/***************************************************************************/
/* DMA operations */
#if 0
static int usdr_allocdma(struct usdr_dev *d, struct usdr_dmabuf *pbufs, unsigned buflen)
{
	int i;
	for (i = 0; i < NUM_DMA_BUFS; i++) {
		pbufs[i].kvirt = dma_alloc_attrs(&d->pdev->dev, buflen, &pbufs[i].phys, GFP_KERNEL, DMA_ATTR_NO_KERNEL_MAPPING);
		if (!pbufs[i].kvirt) {
			printk(KERN_INFO PFX "Failed to allocate %d DMA buffer", i);
			for (; i >= 0; --i) {
				dma_free_attrs(&d->pdev->dev, buflen, pbufs[i].kvirt, pbufs[i].phys, DMA_ATTR_NO_KERNEL_MAPPING);
			}
			return -1;
		}

		printk(KERN_NOTICE PFX "buf[%d]=%lx [virt %p]\n", i, (unsigned long)pbufs[i].phys, pbufs[i].kvirt);
	}
	return 0;
}

static void usdr_freedma(struct usdr_dev *d, struct usdr_dmabuf *pbufs, unsigned buflen)
{
	int i;
	for (i = 0; i < NUM_DMA_BUFS; i++) {
		dma_free_attrs(&d->pdev->dev, buflen, pbufs[i].kvirt, pbufs[i].phys, DMA_ATTR_NO_KERNEL_MAPPING);
	}
}
#endif

static int init_bucket(struct usdr_dev *dev)
{
    unsigned i;
    for (i = 0; i < dev->dl.bucket_count; i++) {
        struct notification_bucket* b = &dev->buckets[i];

        b->db.kvirt = dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE, &b->db.phys,
                                         GFP_KERNEL);
        if(!b->db.kvirt)
        {
            dev_err(&dev->pdev->dev, "Failed to allocate consistent memory (%ld bytes), dma_alloc_coherent() returns NULL!\n", PAGE_SIZE);
            return -ENOMEM;
        }

        b->db.uvirt = 0;
        b->rptr = 0;

        // Initialize to FF so we can see 1->0 toggle on valid entries
        memset(b->db.kvirt, -1, PAGE_SIZE);

        // Writing DMA address resets counter and sets toggle bit to 0
        usdr_reg_wr32(dev,
                      dev->dl.bucket_base, // CFG
                      b->db.phys | i);
        usdr_reg_wr32(dev,
                      dev->dl.bucket_base + 1, //ACK
                      i << 16);
        dev_notice(&dev->pdev->dev, "Bucket %d: DMA at %px to %llx\n", i, b->db.kvirt, b->db.phys);

    }
    return 0;
}

static void deinit_bucket(struct usdr_dev *dev)
{
    unsigned i;
    for (i = 0; i < dev->dl.bucket_count; i++) {
        struct notification_bucket* b = &dev->buckets[i];

        //TODO block interrupt queue
        if(b->db.kvirt)
            dma_free_coherent(&dev->pdev->dev, PAGE_SIZE, b->db.kvirt, b->db.phys);
    }
}

// TODO redefine constants
static irqreturn_t usdr_pcie_irq_bucket_128(int irq, void *data)
{
    struct usdr_dev *d = (struct usdr_dev *)data;
    unsigned i, j, bidx, do_cnf = ~0u;
    uint32_t* bptr;
    struct notification_bucket* b;
    uint64_t wakeups = 0;
    ktime_t ets;

    for (bidx = 0; bidx < d->dl.bucket_count; bidx++) {
        b = &d->buckets[bidx];
        if (b->irq == irq)
            goto irq_found;
    }

    dev_notice(&d->pdev->dev, "IRQ %d: Unknown bucket!\n", irq);
    return IRQ_HANDLED;

irq_found:
    bptr = b->db.kvirt;
    ets = ktime_get();
    for (j = 0, i = b->rptr; i < b->rptr + 256; i++, j++) {
        uint32_t data[4];
        unsigned event_no, flags, l;

        for (l = 0; l < 4; l++)
            data[l] = /*be32_to_cpu*/(bptr[(4 * i + l) & 0x3ff]);

        //event_no = data[3] >> 29;
        //flags = (data[3] & (1u<<28)) ? 1 : 0;

        flags = data[0] >> 31;
        event_no = data[0] & 0x3f;

        if (flags != ((i >> 8) & 1)) {
            break;
        }
        if (i % 32 == 31) {
            do_cnf = (i << 1) & 0x3ff;
        }

        DEBUG_DEV_OUT(&d->pdev->dev, "BUCKET %d IRQ %d: Event %d Flag: %d; RPTR %d; Data: %08x_%08x_%08x_%08x\n",
                   i, irq, event_no, flags, b->rptr, data[3], data[2], data[1], data[0]);


        // TODO: based on event handler process data
        if (event_no == 0 || event_no == 1) {
            unsigned k = (d->streaming[event_no].stat_wptr) & (STAT_MAX_SZ - 1);
            d->streaming[event_no].stat_data[4 * k + 0] = data[1]; // 0
            d->streaming[event_no].stat_data[4 * k + 1] = data[2]; // 1
            d->streaming[event_no].stat_data[4 * k + 2] = data[3]; // 2
            d->streaming[event_no].stat_data[4 * k + 3] =
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
                 ets.tv64;     // Timestamp
#else
                 ets;
#endif

            d->streaming[event_no].stat_wptr++;

            //dev_notice(&d->pdev->dev, "BUCKET %d IRQ %d: Event %d Flag: %d; RPTR %d; Data: %08x_%08x_%08x_%08x %016llx\n",
            //       i, irq, event_no, flags, b->rptr, data[3], data[2], data[1], data[0], ets);
        } else {
            d->rb_ev_data[event_no] = data[1]; //0
        }
        atomic_inc(&d->irq_ev_cnt[event_no]);
        //wake_up_interruptible(&d->irq_ev_wq[event_no]);
        wakeups |= (1u << event_no);
    }

    if (j > 1) {
        DEBUG_DEV_OUT(&d->pdev->dev, "BUCKET %d IRQ %d: rptr = %d int suppressed %d times\n", i, irq, b->rptr, j);
    }

    for (j = 0; j < MAX_INT; j++) {
        if (wakeups & (1u << j)) {
            wake_up_interruptible(&d->irq_ev_wq[j]);
        }
    }

    b->rptr = i & 0x1ff;

    //dev_notice(&d->pdev->dev, "BUCKET %d IRQ %d: rptr = %d\n", i, irq, b->rptr);

    if (do_cnf != ~0u) {
        //Send confirmation pointer
        usdr_reg_wr32(d,
                      d->dl.bucket_base + 1, //ACK
                      (bidx << 16) | do_cnf);
    }
    return IRQ_HANDLED;
}

/***************************************************************************/
/* File operations */
/***************************************************************************/

static int usdrfd_open(struct inode *inode, struct file *filp)
{
        struct usdr_dev *dev;
	unsigned long flags;
	int granted = 0;

        dev = container_of(inode->i_cdev, struct usdr_dev, cdev);
	filp->private_data = dev;

	spin_lock_irqsave(&dev->slock, flags);
        if ((dev->dev_mask & DEV_EXCLUSIVE) == 0) {
                dev->dev_mask |= DEV_EXCLUSIVE;
		granted = 1;
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	if (granted && dev->pdev->error_state != pci_channel_io_normal) {
		dev_err(&dev->pdev->dev, "PCI device /dev/usdr%d is disconnected or in error state\n", dev->devno);

		spin_lock_irqsave(&dev->slock, flags);
		dev->dev_mask &= ~DEV_EXCLUSIVE;
		spin_unlock_irqrestore(&dev->slock, flags);

		return -EIO;
	}

	return (granted) ? 0 : -EBUSY;
}

static int usdrfd_release(struct inode *inode, struct file *filp)
{
        struct usdr_dev *usdrdev = filp->private_data;
	unsigned long flags;

        if ((usdrdev->dev_mask & DEV_VALID) == 0) {
                printk(KERN_INFO PFX "usdr:%d dev is invalid!\n", usdrdev->devno);
		return 0;
	}

        spin_lock_irqsave(&usdrdev->slock, flags);
        usdrdev->dev_mask &= ~DEV_EXCLUSIVE;
        spin_unlock_irqrestore(&usdrdev->slock, flags);

	return 0;
}

static ssize_t usdrfd_read(struct file *filp, char __user *buf, size_t count,
                           loff_t *f_pos)
{
    return -EINVAL;
}

static ssize_t usdrfd_write(struct file *filp, const char __user *buf, size_t count,
                            loff_t *f_pos)
{
    return -EINVAL;
}

static __poll_t usdrfd_poll(struct file *filp, poll_table *wait)
{
    struct usdr_dev *usdrdev = filp->private_data;
    __poll_t events = 0;
    int poll_event_rd = usdrdev->dl.poll_event_rd;
    int poll_event_wr = usdrdev->dl.poll_event_wr;

    if (!usdrdev->irq_configured)
        return 0;

    if (poll_event_rd >= 0) {
        poll_wait(filp, &usdrdev->irq_ev_wq[poll_event_rd], wait);

        if (atomic_read(&usdrdev->irq_ev_cnt[poll_event_rd]))
            events |= EPOLLIN;
    }

    if (poll_event_wr >= 0) {
        poll_wait(filp, &usdrdev->irq_ev_wq[poll_event_wr], wait);

        if (atomic_read(&usdrdev->irq_ev_cnt[poll_event_wr]))
            events |= EPOLLOUT;
    }

	return events;
}

static int usdr_stream_free(struct usdr_dev *usdrdev, unsigned sno)
{
    unsigned i;
    struct stream_state* s;
    if (sno >= usdrdev->dl.streams_count)
        return -EINVAL;

    s = usdrdev->streams[sno];
    if (!s)
        return 0;

    // Release DMA buffers
    if (usdrdev->dl.stream_core[sno] == USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD)) {
        usdr_reg_wr32(usdrdev, usdrdev->dl.stream_cfg_base[sno] + s->dma_buffs, 0);
    } else {
        usdr_reg_wr32(usdrdev, usdrdev->dl.stream_cfg_base[sno] + 7 * s->dma_buffs, 0);
    }

    // Destroy buffers
    for (i = s->dma_buffs; i > 0; i--) {
        dma_free_attrs(&usdrdev->pdev->dev,
                       s->dma_buff_size,
                       s->dmab[i - 1].kvirt,
                       s->dmab[i - 1].phys,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
                       &s->dma_attr);
#else
                       s->dma_buffer_flags);
#endif

    }

    kfree(s);

    usdrdev->streams[sno] = 0;
    return 0;
}

static int usdr_device_initialie(struct usdr_dev *usdrdev)
{
    unsigned i, irq, directirqs, mxsps;
    int res;

    //Sanity check
    if ((usdrdev->dl.spi_cnt > MAX_SPI_COUNT) ||
            (usdrdev->dl.i2c_cnt > MAX_I2C_COUNT) ||
            (usdrdev->dl.idx_regsp_cnt > MAX_INDEXED_SPACES) ||
            (usdrdev->dl.interrupt_count > MAX_INT))  {

        dev_err(&usdrdev->pdev->dev, "DEVINIT: Incorrect count values\n");
        return -EINVAL;
    }

    mxsps = 0xffffff;
    for (i = usdrdev->dl.idx_regsp_cnt; i > 0; i--) {
        if (usdrdev->dl.idx_regsp_vbase[i - 1] > mxsps) {
            dev_err(&usdrdev->pdev->dev, "DEVINIT: IDXSPS%d incorrect order!\n", i - 1);
            return -EINVAL;
        }
        mxsps = usdrdev->dl.idx_regsp_vbase[i - 1];
        dev_notice(&usdrdev->pdev->dev,
                   "IDX_REG_MAP[%d]  %06x -> %03x\n",
                   i - 1,
                   usdrdev->dl.idx_regsp_vbase[i - 1],
                   usdrdev->dl.idx_regsp_base[i - 1]);
    }
    if (mxsps < 0x100) {
        dev_err(&usdrdev->pdev->dev, "IO space shrinked to %02x, probably an error in the config!\n",
                mxsps);
        return -EINVAL;
    }

    memset(&usdrdev->irq_funcs, 0, sizeof(usdrdev->irq_funcs));
//    memset(&usdrdev->irq_param, 0, sizeof(usdrdev->irq_param));
    memset(&usdrdev->irq_ev_cnt, 0, sizeof(usdrdev->irq_ev_cnt));

    for (i = 0; i < usdrdev->dl.interrupt_count; i++) {
        init_waitqueue_head(&usdrdev->irq_ev_wq[i]);
    }

#ifdef OLD_IRQ
    //Initialize interrupt routines
    res = pci_alloc_irq_vectors(usdrdev->pdev, 1, usdrdev->dl.interrupt_count, PCI_IRQ_MSI);
    if (res < 1) {
        dev_err(&usdrdev->pdev->dev, "Unable to initialize interrupts: %d\n", res);
        return res;
    }
    usdrdev->irq_configured = res; // Number of interrupts configured
    directirqs = res;

    dev_notice(&usdrdev->pdev->dev, "Interrupts configured %d, requested %d\n",
               usdrdev->irq_configured, usdrdev->dl.interrupt_count);

    if (usdrdev->irq_configured < usdrdev->dl.interrupt_count) {
        irq = usdrdev->irq_configured - 1;
        // muxed interrupts
        snprintf(&usdrdev->int_names[INTNAMES_MAX * irq], INTNAMES_MAX, "usdr%d_mux", usdrdev->devno);
        res = request_irq(pci_irq_vector(usdrdev->pdev, irq), //usdrdev->pdev->irq + irq,
                          usdr_pcie_irq_muxed, 0, &usdrdev->int_names[INTNAMES_MAX * irq], usdrdev);
        if (res) {
            dev_err(&usdrdev->pdev->dev, "DEVINIT: requested MUX INT%d failed!\n", irq);
            goto failed_cfg_mux;
        }

        usdrdev->irq_funcs[irq] = usdr_pcie_irq_muxed;
        //usdrdev->irq_param[irq] = usdrdev;

        directirqs--;
    }

    for (i = 0; i < usdrdev->dl.spi_cnt; i++) {
        //init_waitqueue_head(&usdrdev->state_spi[i].whead);
        irq = usdrdev->dl.spi_int_number[i];
        if (irq >= usdrdev->dl.interrupt_count) {
            dev_err(&usdrdev->pdev->dev, "DEVINIT: SPI%d asks for IRQ%d!\n",
                    i, irq);

            res = -EINVAL;
            goto failed_cfg_ints;
        }

        //usdrdev->state_spi[i].dev = usdrdev;
        //usdrdev->state_spi[i].irqn = irq;

        if (irq < directirqs) {
            snprintf(&usdrdev->int_names[INTNAMES_MAX * irq], INTNAMES_MAX, "usdr%d_spi%d", usdrdev->devno, i);
            res = request_irq(pci_irq_vector(usdrdev->pdev, irq), //usdrdev->pdev->irq + irq,
                              usdr_pcie_irq_event, 0, &usdrdev->int_names[INTNAMES_MAX * irq], usdrdev);
            if (res) {
                dev_err(&usdrdev->pdev->dev, "DEVINIT: requested SPI INT%d failed!\n", irq);
                goto failed_cfg_ints;
            }
        }

        usdrdev->irq_funcs[irq] = usdr_pcie_irq_event;
        //usdrdev->irq_param[irq] = &usdrdev->state_spi[i];
    }

    for (i = 0; i < usdrdev->dl.i2c_cnt; i++) {
        //init_waitqueue_head(&usdrdev->state_i2c[i].whead);
        irq = usdrdev->dl.i2c_int_number[i];
        if (irq >= usdrdev->dl.interrupt_count) {
            dev_err(&usdrdev->pdev->dev, "DEVINIT: I2C%d asks for IRQ%d!\n",
                    i, irq);
            res = -EINVAL;
            goto failed_cfg_ints;
        }

        //usdrdev->state_i2c[i].dev = usdrdev;
        //usdrdev->state_i2c[i].irqn = irq;

        if (irq < directirqs) {
            snprintf(&usdrdev->int_names[INTNAMES_MAX * irq], INTNAMES_MAX, "usdr%d_i2c%d", usdrdev->devno, i);
            res = request_irq(pci_irq_vector(usdrdev->pdev, irq), //usdrdev->pdev->irq + irq,
                              usdr_pcie_irq_event, 0, &usdrdev->int_names[INTNAMES_MAX * irq], usdrdev);
            if (res) {
                dev_err(&usdrdev->pdev->dev, "DEVINIT: requested I2C INT%d failed!\n", irq);
                goto failed_cfg_ints;
            }
        }

        usdrdev->irq_funcs[irq] = usdr_pcie_irq_event; //usdr_pcie_irq_i2c;
        //usdrdev->irq_param[irq] = &usdrdev->state_i2c[i];
    }

    //Initialize general event slots
    for (i = 0; i < usdrdev->dl.interrupt_count; i++) {
        if (usdrdev->irq_funcs[i] == NULL) {
            if (i < directirqs) {
                irq = i;
                snprintf(&usdrdev->int_names[INTNAMES_MAX * irq], INTNAMES_MAX, "usdr%d_event%d", usdrdev->devno, i);
                res = request_irq(pci_irq_vector(usdrdev->pdev, irq), //usdrdev->pdev->irq + i,
                                  usdr_pcie_irq_event, 0, &usdrdev->int_names[INTNAMES_MAX * irq], usdrdev);
                if (res) {
                    dev_err(&usdrdev->pdev->dev, "DEVINIT: requested EVENT INT%d failed!\n", irq);
                    goto failed_cfg_ints;
                }
            }

            usdrdev->irq_funcs[i] = usdr_pcie_irq_event;
            //usdrdev->irq_param[irq] = usdrdev;
        }
    }

    uint32_t c = usdr_reg_rd32(usdrdev, 0);
    uint32_t imsk = (1u << (usdrdev->dl.interrupt_count)) - 1;
    //imsk = imsk & (~2u);

    //usdr_reg_wr32(usdrdev, usdrdev->dl.interrupt_base, imsk);
    for (i = 0; i < usdrdev->dl.interrupt_count; i++) {
        usdr_reg_wr32(usdrdev,  usdrdev->dl.interrupt_base,  i | (i << 8) | (0 << 16) | (7 << 20));
    }
    dev_notice(&usdrdev->pdev->dev, "Device initialized, spi buses %d, i2c buses %d, indexed %d, interrupts %d   %08x DMSK %08x\n",
               usdrdev->dl.spi_cnt,
               usdrdev->dl.i2c_cnt,
               usdrdev->dl.idx_regsp_cnt,
               usdrdev->dl.interrupt_count, c, imsk);
#else

    dev_notice(&usdrdev->pdev->dev, "Device initialized, spi buses %d, i2c buses %d, indexed %d, bucket mode\n",
               usdrdev->dl.spi_cnt,
               usdrdev->dl.i2c_cnt,
               usdrdev->dl.idx_regsp_cnt);

    //init_bucket(usdrdev);


    //Initialize interrupt routines
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,11,0)
    res = pci_enable_msi_range(usdrdev->pdev, 1, 1);
#else
    res = pci_alloc_irq_vectors(usdrdev->pdev, 1, 1, PCI_IRQ_MSI);
#endif
    if (res < 1) {
        dev_err(&usdrdev->pdev->dev, "pci_alloc_irq_vectors: unable to initialize interrupts: %d\n", res);
        return res;
    }

    // Configure Bucket 0
    irq = 0;
    snprintf(&usdrdev->int_names[INTNAMES_MAX * irq], INTNAMES_MAX, "usdr%d_b%d", usdrdev->devno, 0);
    res = request_irq(pci_irq_vector(usdrdev->pdev, irq),
                        usdr_pcie_irq_bucket_128, 0, &usdrdev->int_names[INTNAMES_MAX * irq], usdrdev);
    if (res) {
        dev_err(&usdrdev->pdev->dev, "DEVINIT: requested interrupt %s failed!\n", &usdrdev->int_names[INTNAMES_MAX * irq]);
        goto failed_cfg_mux;
    }
    usdrdev->buckets[0].irq = pci_irq_vector(usdrdev->pdev, irq);

    //Configure all IRQs to report to bucket 0 via interrupt 0
    for (i = 0; i < 32; i++) {
        usdr_reg_wr32(usdrdev, usdrdev->dl.interrupt_base, i | (0 << 8) | (1 << 16) | (0 << 20));
        usdrdev->irq_funcs[i] = usdr_pcie_irq_event;
    }
    usdrdev->irq_configured = 1;

#endif
    return 0;

#ifdef OLD_IRQ
failed_cfg_ints:
    for (i = 0; i < usdrdev->dl.interrupt_count; i++) {
        if (usdrdev->irq_funcs[i] != NULL) {
            free_irq(pci_irq_vector(usdrdev->pdev, irq), //usdrdev->pdev->irq + i,
                     usdrdev);//usdrdev->irq_param[i]);
        }
    }
#endif
failed_cfg_mux:
    pci_disable_msi(usdrdev->pdev);
    return res;
}

static int usdr_stream_initialize(struct usdr_dev *usdrdev,
                                  struct pcie_driver_sdma_conf *sdma)
{
    struct stream_state* s;
    unsigned min_bufs, max_bufs;
    unsigned max_bufsz;
    unsigned i;
    unsigned newsz, flags;
    unsigned sno = sdma->sno;
    if (sno >= usdrdev->dl.streams_count)
        return -EBADSLT;

    min_bufs = 1u << ((usdrdev->dl.stream_cap[sno] >> STREAM_CAP_MINBUFS_OFF) & STREAM_CAP_SZ_MSK);
    max_bufs = 1u << ((usdrdev->dl.stream_cap[sno] >> STREAM_CAP_MAXBUFS_OFF) & STREAM_CAP_SZ_MSK);
    max_bufsz = 4096u << ((usdrdev->dl.stream_cap[sno] >> STREAM_CAP_MAXBUFSZ_OFF) & STREAM_CAP_SZ_MSK);

    if (sdma->dma_buf_sz == 0) {
        // Use maximum available
        sdma->dma_buf_sz = max_bufsz;
    }

    if (sdma->dma_bufs < min_bufs || sdma->dma_bufs > max_bufs || sdma->dma_buf_sz > max_bufsz)
        return -EINVAL;

    newsz = (sdma->dma_buf_sz + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
    flags = 0;

    if ((s = usdrdev->streams[sno])) {
        if ((s->dma_buffs == sdma->dma_bufs) &&
                (s->dma_buff_size >= newsz) &&
                (s->dma_buffer_flags == flags)) {

            goto exit_success;
        }

        // Reinitialize buffers
        usdr_stream_free(usdrdev, sno);
    }

    s = (struct stream_state*)kzalloc(sizeof(*s) + (sdma->dma_bufs * sizeof(struct usdr_dmabuf)), GFP_KERNEL);
    if (!s)
        return -ENOMEM;

    s->dma_buffs = sdma->dma_bufs;
    s->dma_buff_size = newsz;
    s->dma_buffer_flags = flags;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
    init_dma_attrs(&s->dma_attr);
#endif

    for (i = 0; i < sdma->dma_bufs; i++) {
            s->dmab[i].kvirt = dma_alloc_attrs(&usdrdev->pdev->dev,
                                             s->dma_buff_size,
                                             &s->dmab[i].phys,
                                             GFP_KERNEL,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
                                             s->dma_buffer_flags);
#else
                                             &s->dma_attr);
#endif
            if (!s->dmab[i].kvirt) {
                    printk(KERN_INFO PFX "Failed to allocate %d DMA buffer", i);
                    goto failed_alloc;
            }
            s->dmab[i].uvirt = NULL;
            //dev_notice(&usdrdev->pdev->dev, "buf[%d]=%lx len=%d [virt %px]\n", i,
            //           (unsigned long)s->dmab[i].phys, s->dma_buff_size, s->dmab[i].kvirt);
    }

    // Initialize dma buffer pointer in the dev
    if (usdrdev->dl.stream_core[sno] == USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_RXDMA_BRSTN) ||
    	usdrdev->dl.stream_core[sno] == USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD) ) {
        for (i = 0; i < sdma->dma_bufs; i++) {
            usdr_reg_wr32(usdrdev,
                          usdrdev->dl.stream_cfg_base[sno] + i,
                          s->dmab[i].phys);
            // dev_notice(&usdrdev->pdev->dev, "cfg=%08x\n", usdrdev->dl.stream_cfg_base[sno] + i);
        }
    } else {
        dev_err(&usdrdev->pdev->dev, "Unknown stream core: %08x\n",
                usdrdev->dl.stream_core[sno]);
        goto failed_alloc;
    }

    usdrdev->streams[sno] = s;
exit_success:
    //s->cntr_last = 0;

    sdma->out_vma_length = s->dma_buff_size * s->dma_buffs;
    sdma->out_vma_off = ((off_t)(sdma->sno + 1)) << VMA_STREAM_IDX_SHIFT;

    // Flush non-read events
    usdrdev->streaming[sno].stat_rptr = usdrdev->streaming[sno].stat_wptr;
    usdrdev->streams[sno]->abuffer_no = 0;
    usdrdev->streams[sno]->bbuffer_no = 0;

    if (usdrdev->dl.stream_core[sno] == USDR_MAKE_COREID(USDR_CS_STREAM, USDR_SC_TXDMA_OLD)) {
    	// Put all available buffers, no OOB data for the first N buffs
    	atomic_xchg(&usdrdev->irq_ev_cnt[usdrdev->dl.stream_int_number[sno]], sdma->dma_bufs);

        // Set DMA engine maximum buffer limit
        usdr_reg_wr32(usdrdev,
                      usdrdev->dl.stream_cfg_base[sno] + sdma->dma_bufs,
                      sdma->dma_buf_sz - 1);
        dev_notice(&usdrdev->pdev->dev, "TX stream is limited to %d bytes\n", sdma->dma_buf_sz);
    } else {
    	// Clear spurious interrupts
    	atomic_xchg(&usdrdev->irq_ev_cnt[usdrdev->dl.stream_int_number[sno]], 0);

        usdr_reg_wr32(usdrdev,
                      usdrdev->dl.stream_cfg_base[sno] + 7 * sdma->dma_bufs,
                      sdma->dma_buf_sz - 1);
        dev_notice(&usdrdev->pdev->dev, "RX stream is limited to %d bytes\n", sdma->dma_buf_sz);
    }
    return 0;

failed_alloc:
    for (; i > 0; i--) {
        dma_free_attrs(&usdrdev->pdev->dev,
                       s->dma_buff_size,
                       s->dmab[i - 1].kvirt,
                       s->dmab[i - 1].phys,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
                       &s->dma_attr);
#else
                       s->dma_buffer_flags);
#endif
    }
    kfree(s);
    return -ENOMEM;
}

static int validate_snoto(struct usdr_dev *usdrdev, unsigned long snomskto, unsigned* to, unsigned* sno)
{
    unsigned timeoutms = snomskto >> 8;
    unsigned streamno = snomskto & 0xff;

    if (streamno >= usdrdev->dl.streams_count) {
        dev_err(&usdrdev->pdev->dev, "validate_snoto %d stream is invalid!\n", streamno);
        return -EINVAL;
    }

    if (!usdrdev->streams[streamno]) {
        dev_err(&usdrdev->pdev->dev, "validate_snoto %d stream isn't configured!\n", streamno);
        return -EIO;
    }

    *to = timeoutms;
    *sno = streamno;
    return 0;
}

static int usdr_stream_wait_or_alloc(struct usdr_dev *usdrdev, unsigned long snomskto,
                                     void* oob_out, unsigned *oob_length, int nonblock, int op_wait)
{
    int res;
    unsigned sno, to, to_hz;
    unsigned eno;
    unsigned cnt; //, dcnt;
    //unsigned cntr_last;
    uint64_t i;
    uint64_t max;
    unsigned oobcnt = 0, ooidx = 0;
    uint64_t* oob_out_u64 = oob_out;
    unsigned oob_cnt_max = (oob_length) ? *oob_length / 16 : 0;

    res = validate_snoto(usdrdev, snomskto, &to, &sno);
    if (res)
        return res;

    eno = usdrdev->dl.stream_int_number[sno];

    if (nonblock/*file->f_flags & O_NONBLOCK*/) {
        cnt = atomic_xchg(&usdrdev->irq_ev_cnt[eno], 0);
        if (cnt == 0)
            return -EAGAIN;
    } else {
        //Return buffno if available; do cache flash if needed
        to_hz = to * HZ / 1000;
        res = wait_event_interruptible_timeout(usdrdev->irq_ev_wq[eno],
                                            (cnt = atomic_xchg(&usdrdev->irq_ev_cnt[eno], 0)) != 0,
                                            to_hz);
        if (res == 0) {
            BUG_ON(cnt != 0);
            return -ETIMEDOUT;
        } else if (res < 0) {
            return res;
        } else {
            BUG_ON(cnt == 0);
        }
    }

    max = usdrdev->streaming[sno].stat_wptr;
    if (max > usdrdev->streaming[sno].stat_rptr + cnt) {
	max = usdrdev->streaming[sno].stat_rptr + cnt;
    }

    for (i = usdrdev->streaming[sno].stat_rptr; i < max; i++, ooidx++) {
        unsigned k, nreg[3], ktm;
        k = (usdrdev->streaming[sno].stat_rptr) & (STAT_MAX_SZ - 1);
        nreg[0] = usdrdev->streaming[sno].stat_data[4 * k + 0];
        nreg[1] = usdrdev->streaming[sno].stat_data[4 * k + 1];
        nreg[2] = usdrdev->streaming[sno].stat_data[4 * k + 2];
        ktm = usdrdev->streaming[sno].stat_data[4 * k + 3];

        //sr = ((uint64_t)nreg[1] << 32) | nreg[0];
        usdrdev->streaming[sno].stat_rptr++;

        //cntr_last = (stat >> 8) & 0x3f;
        //dcnt = (cntr_last - usdrdev->streams[sno]->cntr_last) & 0x3f;
        //BUG_ON(dcnt == 0);
        //if (dcnt == 0) {
        //    dev_err(&usdrdev->pdev->dev, "dnct = %d, stat = %08x sr=%016llx\n", dcnt, stat, sr);
        //}
        //usdrdev->streams[sno]->cntr_last = cntr_last;

        if (ooidx < oob_cnt_max) {
            // timestamp in NS, so will wrap every ~4s, which is enough to calculate jitter between calls
            oob_out_u64[2 * ooidx + 0] = (((uint64_t)nreg[1]) << 32) | nreg[0];
            oob_out_u64[2 * ooidx + 1] = (((uint64_t)ktm) << 32) | nreg[2];
            oobcnt++;

            // dw0 - evnt[1] - reg[0]
            // dw1 - evnt[2] - reg[1]
            // dw2 - evnt[0] - reg[2]
            // dw3 - timestamp
        }
    }

    if (oob_length) {
    	*oob_length = oobcnt * 16;
    }

    // flush_cache_range(vma, start, end);
    if (op_wait && ((usdrdev->dev_mask & DEV_NO_DMA_SYNC) == 0)) {
        for (i = 0; i < cnt; i++) {
            u64 bno = usdrdev->streams[sno]->abuffer_no + i;
            unsigned idx = bno % usdrdev->streams[sno]->dma_buffs;

            //dev_err(&usdrdev->pdev->dev, "Buffer_W %ld - %d \n", (long)bno, idx);
            dma_sync_single_for_cpu(&usdrdev->pdev->dev,
                                    usdrdev->streams[sno]->dmab[idx].phys,
                                    usdrdev->streams[sno]->dma_buff_size, DMA_FROM_DEVICE);
        }
        usdrdev->streams[sno]->abuffer_no += cnt;
    }

    //dma_sync_single_for_cpu()
    // TODO FLUSH CACHE on non-coherent devices
    // TODO: this works only on non-muxed interrupts!!!
    //BUG_ON(cnt > 0xff);
    //bptr = usdrdev->streams[sno]->cores.rxbrst.bufptr;
    //res = cnt | (bptr << 12);
    //usdrdev->streams[sno]->cores.rxbrst.bufptr = (bptr + cnt) & usdrdev->streams[sno]->cores.rxbrst.bmsk;
    //return res;

    return cnt;
}

static int usdr_stream_release_or_post(struct usdr_dev *usdrdev, unsigned long snomskto, int op_release)
{
    int res;
    unsigned sno, to;
    unsigned cnfbase;

    res = validate_snoto(usdrdev, snomskto, &to, &sno);
    if (res)
        return res;

    if (op_release && ((usdrdev->dev_mask & DEV_NO_DMA_SYNC) == 0)) {
	    u64 bno = usdrdev->streams[sno]->bbuffer_no++;
	    unsigned idx = bno % usdrdev->streams[sno]->dma_buffs;

        //dev_err(&usdrdev->pdev->dev, "Buffer_R %ld - %d\n", (long)bno, idx);
	    dma_sync_single_for_device(&usdrdev->pdev->dev,
                                   usdrdev->streams[sno]->dmab[idx].phys,
                                   usdrdev->streams[sno]->dma_buff_size, DMA_TO_DEVICE);
    }

    cnfbase = usdrdev->dl.stream_cnf_base[sno];
    usdr_reg_wr32(usdrdev, cnfbase, to);
    return 0;
}


static int usdr_wait_event(struct usdr_dev *usdrdev,
                           unsigned event_no,
                           unsigned timeout_ms)
{
    unsigned to_hz = timeout_ms * HZ / 1000;
    unsigned cnt;
    int res = wait_event_interruptible_timeout(usdrdev->irq_ev_wq[event_no],
                                               (cnt = atomic_xchg(&usdrdev->irq_ev_cnt[event_no], 0)) != 0,
                                               to_hz);
    if (res == 0) {
        return -ETIMEDOUT;
    } else if (res < 0) {
        return res;
    }

    BUG_ON(cnt > 0xff);
    return cnt;
}

static long usdrfd_ioctl(struct file *filp,
			 unsigned int ioctl_num,/* The number of the ioctl */
			 unsigned long ioctl_param) /* The parameter to it */
{
    int res;
    struct usdr_dev *usdrdev = filp->private_data;
    void __user *uptr = (void __user *)ioctl_param;

    if (!(usdrdev->dev_mask & DEV_VALID))
        return -EIO;
    if (!(usdrdev->dev_mask & DEV_INITIALIZED)) {
        if (ioctl_num != PCIE_DRIVER_GET_UUID &&
                ioctl_num != PCIE_DRIVER_CLAIM &&
                ioctl_num != PCIE_DRIVER_SET_DEVLAYOUT &&
                ioctl_num != PCIE_DRIVER_CLAIM_VERSION &&
                ioctl_num != PCIE_DRIVER_HWREG_RD32) {

            dev_notice(&usdrdev->pdev->dev, "Device not ready!");
            return -EINVAL;
        }
    }

    switch (ioctl_num) {
    case PCIE_DRIVER_CLAIM_VERSION:
        if (ioctl_param != USDR_DRIVER_ABI_VERSION) {
            dev_err(&usdrdev->pdev->dev, "User requested ABI ver %d, but driver is %d\n",
                    (unsigned)ioctl_param, USDR_DRIVER_ABI_VERSION);
            return -EOPNOTSUPP;
        }
        return 0;

    case PCIE_DRIVER_GET_UUID:
        if (usdrdev->device_data >= (sizeof(s_uuid) / sizeof(s_uuid[0])))
		return -EFAULT;
        if (copy_to_user(uptr, &s_uuid[usdrdev->device_data], sizeof(s_uuid[usdrdev->device_data])))
                return -EFAULT;
        return 0;

    case PCIE_DRIVER_CLAIM:

        // Reset I2C cache state
        memset(usdrdev->i2cc, 0, sizeof(usdrdev->i2cc));
        memset(usdrdev->i2clut, 0, sizeof(usdrdev->i2clut));
        return 0;

    case PCIE_DRIVER_SET_DEVLAYOUT:
        if (!(usdrdev->dev_mask & DEV_INITIALIZED)) {

            if (copy_from_user(&usdrdev->dl, uptr, sizeof(usdrdev->dl)))
                    return -EFAULT;

            res = usdr_device_initialie(usdrdev);
            if (res)
                    return res;

            usdrdev->dev_mask |= DEV_INITIALIZED;
#ifdef CONFIG_X86
            usdrdev->dev_mask |= DEV_NO_DMA_SYNC;
#endif
        }
        return 0;
    case PCIE_DRIVER_HWREG_RD32: {
        struct pcie_driver_hwreg32 rop;
        if (copy_from_user(&rop, uptr, sizeof(unsigned)))
                return -EFAULT;
        if (rop.addr > 1023)
            return -EIO;

        rop.value = usdr_readl(usdrdev, rop.addr);

        DEBUG_DEV_OUT(&usdrdev->pdev->dev, "HWREG_RD32[%x] -> %x\n", rop.addr, rop.value);
        if (copy_to_user(uptr + sizeof(unsigned), &rop.value, sizeof(rop.value)))
                return -EFAULT;

        return 0;
    }
    case PCIE_DRIVER_HWREG_WR32: {
        struct pcie_driver_hwreg32 rop;
        if (copy_from_user(&rop, uptr, sizeof(rop)))
                return -EFAULT;
        if (rop.addr > 1023)
            return -EIO;

        DEBUG_DEV_OUT(&usdrdev->pdev->dev, "HWREG_WR32[%x] <- %x\n", rop.addr, rop.value);
        usdr_writel(usdrdev, rop.addr, rop.value);
        return 0;
    }
    case PCIE_DRIVER_HWREG_RD64: {
        struct pcie_driver_hwreg64 rop;
        if (copy_from_user(&rop, uptr, sizeof(unsigned)))
                return -EFAULT;
        if (rop.addr > 1023)
            return -EIO;

        rop.value = usdr_readq(usdrdev, rop.addr);

        DEBUG_DEV_OUT(&usdrdev->pdev->dev, "HWREG_RD64[%x] -> %llx\n", rop.addr, rop.value);
        if (copy_to_user(uptr + sizeof(unsigned), &rop.value, sizeof(rop.value)))
                return -EFAULT;

        return 0;
    }
    case PCIE_DRIVER_HWREG_WR64: {
        struct pcie_driver_hwreg64 rop;
        if (copy_from_user(&rop, uptr, sizeof(rop)))
                return -EFAULT;
        if (rop.addr > 1023)
            return -EIO;

        DEBUG_DEV_OUT(&usdrdev->pdev->dev, "HWREG_WR64[%x] <- %llx\n", rop.addr, rop.value);
        usdr_writeq(usdrdev, rop.addr, rop.value);
        return 0;
    }
    case PCIE_DRIVER_SPI32_TRANSACT: {
        struct pcie_driver_spi32 sp;
        unsigned core, base, irq, cnt, busno;

        if (copy_from_user(&sp, uptr, sizeof(sp)))
                return -EFAULT;
        busno = SPIEXT_LSOP_GET_BUS(sp.buscfg);
        if (busno >= usdrdev->dl.spi_cnt)
            return -EINVAL;

        core = usdrdev->dl.spi_core[busno];
        base = usdrdev->dl.spi_base[busno];
        irq = usdrdev->dl.spi_int_number[busno];

        if (core == SPI_CORE_32W) {
            usdr_reg_wr32(usdrdev, base, sp.dw_io);
        } else if (core == SPI_CORE_CFGW_CS8) {
            // NOTE: usdr_reg_wr64 do cpu_to_be64() which reverse DWORD order on PCIe bus
            __u64 cmd = (((__u64)SPIEXT_LSOP_GET_CFG(sp.buscfg)) << 32) | sp.dw_io;
            usdr_reg_wr64(usdrdev, base - 1, cmd);
        } else {
            dev_err(&usdrdev->pdev->dev, "SPI%d: core %d isn't supported, update driver!",
                    busno, core);
            return -EINVAL;
        }

        //Wait for completion
        res = wait_event_interruptible_timeout(usdrdev->irq_ev_wq[irq],
                                               (cnt = atomic_xchg(&usdrdev->irq_ev_cnt[irq], 0)) != 0,
                                               HZ);
        if (res == 0) {
            return -ETIMEDOUT;
        } else if (res < 0) {
            return res;
        }
#ifdef OLD_IRQ
        sp.dw_io = usdr_reg_rd32(usdrdev, base);
#else
        sp.dw_io = usdrdev->rb_ev_data[irq];
#endif
        DEBUG_DEV_OUT(&usdrdev->pdev->dev, "SPI%d: Cfg:%08x Rd:%08x cnt:%d\n",
                      busno, sp.buscfg, sp.dw_io, cnt);

        if (copy_to_user(uptr + sizeof(sp.buscfg), &sp.dw_io, sizeof(sp.dw_io)))
            return -EFAULT;

        return 0;
    }
    case PCIE_DRIVER_SI2C_TRANSACT: {
        struct pcie_driver_si2c si2c;
        unsigned i2cinst, i2cbus, i2caddr, core, base, irq, idx, lut, cmd;

        if (copy_from_user(&si2c, uptr, sizeof(si2c)))
                return -EFAULT;

        i2cinst = (si2c.addr >> 24) & 0xFF;
        i2cbus = (si2c.addr >> 16) & 0xFF;
        i2caddr = (si2c.addr) & 0xFFFF;

        if (i2cinst >= usdrdev->dl.i2c_cnt)
            return -EINVAL;

        core = usdrdev->dl.i2c_core[i2cinst];

        if (core != I2C_CORE_AUTO_LUTUPD) {
            dev_err(&usdrdev->pdev->dev, "I2C[%d] core %d isn't supported, update driver!",
                      i2cinst, core);
            return -EINVAL;
        }

        base = usdrdev->dl.i2c_base[i2cinst];
        irq = usdrdev->dl.i2c_int_number[i2cinst];

        // TODO: Protect i2cc structure
        idx = si2c_update_lut_idx(&usdrdev->i2cc[4 * i2cinst], i2caddr, i2cbus);
        lut = si2c_get_lut(&usdrdev->i2cc[4 * i2cinst]);
        res = si2c_make_ctrl_reg(idx, si2c.wrb, si2c.wcnt, si2c.rcnt, &cmd);
        if (res) {
            return res;
        }

        if (usdrdev->i2clut[i2cinst] == lut) {
            usdr_reg_wr32(usdrdev, base, cmd);
        } else {
            // NOTE: usdr_reg_wr64 do cpu_to_be64() which reverse DWORD order on PCIe bus
            __u64 cmd_lut = ((__u64)lut << 32) | cmd;
            usdr_reg_wr64(usdrdev, base - 1, cmd_lut);
            usdrdev->i2clut[i2cinst] = lut;
        }

        DEBUG_DEV_OUT(&usdrdev->pdev->dev, "I2C[%d.%d.%02x] W:%d,R:%d,CMD:%08x,LUT:%08x\n",
                      i2cinst, i2cbus, i2caddr, si2c.wcnt, si2c.rcnt, cmd, lut);

        if (si2c.rcnt > 0) {
            unsigned dout, cnt;

            //Wait for completion
            res = wait_event_interruptible_timeout(usdrdev->irq_ev_wq[irq],
                                                   (cnt = atomic_xchg(&usdrdev->irq_ev_cnt[irq], 0)) != 0,
                                                   HZ);
            if (res == 0) {
                return -ETIMEDOUT;
            } else if (res < 0) {
                return res;
            }
#ifdef OLD_IRQ
            dout = usdr_reg_rd32(usdrdev, base);
#else
            dout = usdrdev->rb_ev_data[irq];
#endif

            si2c.rdb[0] = dout;
            si2c.rdb[1] = (si2c.rcnt > 1) ? (dout >> 8) : 0;
            si2c.rdb[2] = (si2c.rcnt > 2) ? (dout >> 16) : 0;
            si2c.rdb[3] = (si2c.rcnt > 3) ? (dout >> 24) : 0;

            if (copy_to_user(uptr + offsetof(struct pcie_driver_si2c, rdb), si2c.rdb, sizeof(si2c.rdb)))
                return -EFAULT;
        }

        return 0;
    }
    case PCIE_DRIVER_WAIT_SINGLE_EVENT: {
        unsigned event_no = ioctl_param & 0xFF;
        unsigned timeout_ms = ioctl_param >> 8;

        if (event_no >= usdrdev->dl.interrupt_count)
            return -EINVAL;

        return usdr_wait_event(usdrdev, event_no, timeout_ms);
    }
    case PCIE_DRIVER_DMA_CONF: {
        struct pcie_driver_sdma_conf sdma;
        if (copy_from_user(&sdma, uptr, sizeof(sdma)))
                return -EFAULT;

        res = usdr_stream_initialize(usdrdev, &sdma);
        if (res)
            return res;

        if (copy_to_user(uptr + offsetof(struct pcie_driver_sdma_conf, dma_buf_sz),
                         &sdma.dma_buf_sz, sizeof(sdma.dma_buf_sz)))
            return -EFAULT;
        if (copy_to_user(uptr + offsetof(struct pcie_driver_sdma_conf, out_vma_off),
                         &sdma.out_vma_off,
                         sizeof(sdma.out_vma_off) + sizeof(sdma.out_vma_length)))
            return -EFAULT;
        return 0;
    }
    case PCIE_DRIVER_DMA_UNCONF:
        return usdr_stream_free(usdrdev, ioctl_param);
    case PCIE_DRIVER_DMA_WAIT:
    case PCIE_DRIVER_DMA_ALLOC:
        return usdr_stream_wait_or_alloc(usdrdev, ioctl_param, NULL, NULL, filp->f_flags & O_NONBLOCK, ioctl_num == PCIE_DRIVER_DMA_WAIT);
    case PCIE_DRIVER_DMA_WAIT_OOB:
    case PCIE_DRIVER_DMA_ALLOC_OOB: {
        struct pcie_driver_woa_oob woaoob;
        unsigned data[128];
        unsigned data_max;
        if (copy_from_user(&woaoob, uptr, sizeof(woaoob)))
                return -EFAULT;

        data_max = (sizeof(data) < woaoob.ooblength) ? sizeof(data) : woaoob.ooblength;
        res = usdr_stream_wait_or_alloc(usdrdev, woaoob.streamnoto, data, &data_max, filp->f_flags & O_NONBLOCK, ioctl_num == PCIE_DRIVER_DMA_WAIT_OOB);

        if (copy_to_user(woaoob.oobdata, data, data_max))
            return -EFAULT;
        if (copy_to_user(uptr + offsetof(struct pcie_driver_woa_oob, ooblength),
                         &data_max,
                         sizeof(woaoob.ooblength)))
            return -EFAULT;

        return res;
    }
    case PCIE_DRIVER_DMA_RELEASE:
    case PCIE_DRIVER_DMA_POST:
        return usdr_stream_release_or_post(usdrdev, ioctl_param, ioctl_num == PCIE_DRIVER_DMA_RELEASE);
    }
    return -EINVAL;
}

static void usdrfd_vma_open(struct vm_area_struct *vma)
{
    printk(KERN_NOTICE PFX "VMA open, virt %lx, phys %lx\n",
           vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

static void usdrfd_vma_close(struct vm_area_struct *vma)
{
    // printk(KERN_NOTICE PFX "VMA close virt %lx\n", vma->vm_start);
}

static struct vm_operations_struct usdrfd_remap_vm_ops = {
    .open =  usdrfd_vma_open,
    .close = usdrfd_vma_close,
};


static int usdrfd_mmap_io(struct usdr_dev *usdrdev, struct vm_area_struct *vma)
{
    unsigned long pfn;
    if (((vma->vm_end - vma->vm_start) >> PAGE_SHIFT) != 1)
        return -EINVAL;

    vma->vm_page_prot = pgprot_device(vma->vm_page_prot);
#ifndef HAVE_VM_FLAGS_SET
    vma->vm_flags |= VM_IO;
#else
    vm_flags_set(vma, VM_IO);
#endif

    pfn = (pci_resource_start(usdrdev->pdev, 0)) >> PAGE_SHIFT;

    if (io_remap_pfn_range(vma, vma->vm_start, pfn,
                           vma->vm_end - vma->vm_start,
                           vma->vm_page_prot))
        return -EAGAIN;

    vma->vm_ops = &usdrfd_remap_vm_ops;
    return 0;
}

static int usdrfd_mmap(struct file *filp, struct vm_area_struct *vma)
{
        struct usdr_dev *usdrdev = filp->private_data;
	unsigned long off;
	unsigned i;
        int err = -ENXIO;
        unsigned streamno = vma->vm_pgoff >> (VMA_STREAM_IDX_SHIFT - PAGE_SHIFT);
        unsigned bno;
        off_t vm_pgoff = vma->vm_pgoff;

        if (!(usdrdev->dev_mask & DEV_VALID))
            return -EIO;
        if (!(usdrdev->dev_mask & DEV_INITIALIZED))
            return -ENXIO;

        if (streamno == 0) {
            return usdrfd_mmap_io(usdrdev, vma);
        }

        if (streamno > usdrdev->dl.streams_count) {
            return -EINVAL;
        }
        --streamno;
        if (!usdrdev->streams[streamno]) {
            return -EACCES;
        }

        bno = (vma->vm_pgoff & ((1ul << (VMA_STREAM_IDX_SHIFT - PAGE_SHIFT)) - 1)) << PAGE_SHIFT;
        if (bno % usdrdev->streams[streamno]->dma_buff_size)
            return -EINVAL;

        bno = bno / usdrdev->streams[streamno]->dma_buff_size;
        if (bno >= usdrdev->streams[streamno]->dma_buffs)
            return -EINVAL;

        //VMA_STREAM_IDX_OFF

        // vma->vm_flags |= VM_LOCKED;
        // vma->vm_start
        // vma->vm_end
        // vma->vm_pgoff

        vma->vm_pgoff = 0;
        err = dma_mmap_attrs(&usdrdev->pdev->dev, vma,
                             usdrdev->streams[streamno]->dmab[bno].kvirt,
                             usdrdev->streams[streamno]->dmab[bno].phys,
                             usdrdev->streams[streamno]->dma_buff_size,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
                             &usdrdev->streams[streamno]->dma_attr);
#else
                             usdrdev->streams[streamno]->dma_buffer_flags);
#endif
        vma->vm_pgoff = vm_pgoff;
        //vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

        //dev_notice(&usdrdev->pdev->dev, "Mapping str%db%d to %lx\n",
        //           streamno, bno, vma->vm_start);

        vma->vm_ops = &usdrfd_remap_vm_ops;
	return err;
}


struct file_operations usdr_fops = {
	.owner =   THIS_MODULE,
        .read =    usdrfd_read,
        .write =   usdrfd_write,
        .unlocked_ioctl = usdrfd_ioctl,
        .open =    usdrfd_open,
        .poll =    usdrfd_poll,
        .mmap =    usdrfd_mmap,
        .release = usdrfd_release,
};


static int usdr_setup_cdev(struct usdr_dev *usdrdev)
{
        dev_t dev_num = usdr_dev_first + usdrdev->devno;

        cdev_init(&usdrdev->cdev, &usdr_fops);
        usdrdev->cdev.owner = THIS_MODULE;
        usdrdev->cdev.ops = &usdr_fops;
        return cdev_add (&usdrdev->cdev, dev_num, 1);
}


/***************************************************************************/
/* Module functions */
/***************************************************************************/
static int usdr_probe(struct pci_dev *pdev,
                const struct pci_device_id *id)
{
        struct usdr_dev* usdrdev;
	int err;
	void __iomem* bar_addr;
    //unsigned usdr_no = devices;
    unsigned usdr_no = find_first_zero_bit(usdr_busy_map, TOT_USDR_DEVS);
	size_t bar_len;

	struct pci_dev *bridge, *tbridge;
	bridge = pci_upstream_bridge(pdev);
	tbridge = (bridge) ? pci_upstream_bridge(bridge) : NULL;

	printk(KERN_INFO PFX "Initializing %s => %s => %s\n", pci_name(pdev), bridge ? pci_name(bridge) : "null", tbridge ? pci_name(tbridge) : "null");
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, "
			"aborting.\n");
		return err;
	}

	pci_set_master(pdev);

	/* Reconfigure MaxReadReq to 1KB */
	pcie_capability_clear_and_set_word(pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_READRQ, PCI_EXP_DEVCTL_READRQ_1024B);

                       if (tbridge) {
		printk(KERN_INFO PFX "Initialize ASM2806 bridge: %px\n", tbridge);
	//	pci_write_config_byte(tbridge, 0xfff, 1); // Switch to GPIO control mode
	//	pci_write_config_byte(tbridge, 0x920, 0x01);
	//	pci_write_config_byte(tbridge, 0x928, 0x00);
	}

	/* Reconfigure MaxReadReq to 4KB */
	//pcie_capability_clear_and_set_word(pdev, PCI_EXP_DEVCTL,
	//				   PCI_EXP_DEVCTL_READRQ, PCI_EXP_DEVCTL_READRQ_4096B);

	//dma_set_mask_and_coherent
	if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		dev_err(&pdev->dev,"No suitable consistent DMA available.\n");
		err = -EINVAL;
		goto err_disable_pdev;
	}

	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		dev_err(&pdev->dev,"No suitable consistent DMA available.\n");
		goto err_disable_pdev;
	}

	bar_len = pci_resource_len(pdev, 0);
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
		bar_len < 1 << 20) {
		dev_err(&pdev->dev, "Missing UL BAR, aborting.\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, "
			"aborting.\n");
		goto err_disable_pdev;
	}

	bar_addr = pci_iomap(pdev, 0, bar_len);
	if (!bar_addr) {
		dev_err(&pdev->dev, "Failed to map CTRL BAR.\n");
		goto err_free_res;
	}

        usdrdev = kzalloc(sizeof(*usdrdev), GFP_KERNEL);
        if (!usdrdev) {
		dev_err(&pdev->dev, "Failed to allocate memory.\n");
		err = -ENOMEM;
		goto err_unmap;
	}
        pci_set_drvdata(pdev, usdrdev);
        usdrdev->bar_addr = bar_addr;
        usdrdev->devno = usdr_no;
        usdrdev->pdev = pdev;
        usdrdev->dev_mask = 0;

        spin_lock_init(&usdrdev->slock);

        usdrdev->dev_mask = DEV_VALID;
        usdrdev->cdevice = device_create(usdr_class,
					 &pdev->dev,
                     MKDEV(MAJOR(usdr_dev_first), MINOR(usdr_dev_first) + usdrdev->devno),
					 NULL,
					 DEVICE_NAME "%d",
                     usdrdev->devno);
        if (IS_ERR(usdrdev->cdevice)) {
		printk(KERN_NOTICE PFX "Unable to register device class\n");
		err = -EINVAL;
		goto failed_device;
	}

	err = usdr_setup_cdev(usdrdev);
	if (err) {
		printk(KERN_NOTICE PFX "Error %d initializing cdev\n", err);
		goto failed_cdev;
	}

	usdrdev->device_data = id->driver_data;

	usdrdev->dl.bucket_count = 1;
	usdrdev->dl.bucket_base = 8;
	err = init_bucket(usdrdev);
	if (err) {
		printk(KERN_NOTICE PFX "Error %d initializing bucket\n", err);
		deinit_bucket(usdrdev);
		goto failed_cdev;
	}

    dev_info(&pdev->dev, "is linked to /dev/usdr%d\n", usdrdev->devno);
    set_bit(usdrdev->devno, usdr_busy_map);

	usdrdev->next = usdr_list;
	usdr_list = usdrdev;
	return 0;

        //cdev_del(&usdrdev->cdev);
failed_cdev:
        device_destroy(usdr_class, MKDEV(MAJOR(usdr_dev_first), MINOR(usdr_dev_first) + usdrdev->devno));
failed_device:
        kfree(usdrdev);
//err_allocdma:
        //usdr_freedma(usdrdev, usdrdev->rxdma, usdrdev->rxdma_bufsize);
err_unmap:
	pci_iounmap(pdev, bar_addr);
err_free_res:
	pci_release_regions(pdev);
err_disable_pdev:
	pci_clear_master(pdev); /* Nobody seems to do this */

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void usdr_remove(struct pci_dev *pdev)
{
    unsigned i;
        struct usdr_dev* usdrdev = pci_get_drvdata(pdev);
	printk(KERN_INFO PFX "Removing device %s\n", pci_name(pdev));

        if (usdrdev->dev_mask & DEV_INITIALIZED) {

            // Disable notification of all events
            for (i = 0; i < 32; i++) {
                // Dispatch ID == 0xf means to ignore this event
                usdr_reg_wr32(usdrdev, usdrdev->dl.interrupt_base, i | (0 << 8) | (0xf << 16) | (0 << 20));
            }

            for (i = 0; i < usdrdev->irq_configured; i++) {
                if (usdrdev->irq_funcs[i] != NULL) {
                    free_irq(pci_irq_vector(pdev, i), //pdev->irq + i,
                             usdrdev); //usdrdev->irq_param[i]);
                }
            }

            pci_disable_msi(pdev);

#ifndef OLD_IRQ
            // Remove bucket memory
            deinit_bucket(usdrdev);
#endif

            for (i = 0; i < usdrdev->dl.streams_count; i++) {
                usdr_stream_free(usdrdev, i);
            }
        }

        cdev_del(&usdrdev->cdev);
        device_destroy(usdr_class, MKDEV(MAJOR(usdr_dev_first), MINOR(usdr_dev_first) + usdrdev->devno));

        usdrdev->dev_mask = 0;

        pci_iounmap(pdev, usdrdev->bar_addr);
	pci_release_regions(pdev);

	pci_clear_master(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);


    //usdr_freedma(usdrdev, usdrdev->rxdma, usdrdev->rxdma_bufsize);
    clear_bit(usdrdev->devno, usdr_busy_map);

    dev_info(&pdev->dev, "is unlinked, /dev/usdr%d removed\n", usdrdev->devno);

    //devices--;
	// TODO: Unchain from list and free the memory
}


static struct pci_device_id usdr_pci_table[] = {
	{ PCI_DEVICE(0x10EE, 0x7032),
	  .driver_data = 0 },
	{ PCI_DEVICE(0x10EE, 0x7031),
	  .driver_data = 0 },
	{ PCI_DEVICE(0x10EE, 0x7044),
	  .driver_data = 1 },
	{ PCI_DEVICE(0x10EE, 0x7045),
	  .driver_data = 2 },
	{ PCI_DEVICE(0x10EE, 0x7046),
	  .driver_data = 3 },
	{ PCI_DEVICE(0x10EE, 0x7049),
	  .driver_data = 4 },
	{ PCI_DEVICE(0x10EE, 0x9049),
	  .driver_data = 4 },
	{ PCI_DEVICE(0x10EE, 0x9034),
	  .driver_data = 5 },
	{ PCI_DEVICE(0x10EE, 0x9044),
	  .driver_data = 5 },
	{ PCI_DEVICE(0x10EE, 0x7071),
	  .driver_data = 6 },
	{ 0, }
};

//static struct pci_device_id xmass_pci_table[] = {
//	{ PCI_DEVICE(0x1B21, 0x2806), .driver_data = 0 },
//	{ 0, }
//};


static int xmassfd_open(struct inode *inode, struct file *filp)
{
        struct xmass_dev *dev;
	unsigned long flags;
	int granted = 0;

        dev = container_of(inode->i_cdev, struct xmass_dev, cdev);
	filp->private_data = dev;

	spin_lock_irqsave(&dev->slock, flags);
        if ((dev->dev_mask & DEV_EXCLUSIVE) == 0) {
                dev->dev_mask |= DEV_EXCLUSIVE;
		granted = 1;
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	return (granted) ? 0 : -EBUSY;
}

static int xmassfd_release(struct inode *inode, struct file *filp)
{
        struct xmass_dev *dev = filp->private_data;
	unsigned long flags;

        if ((dev->dev_mask & DEV_VALID) == 0) {
                printk(KERN_INFO XPFX "xmass:%d dev is invalid!\n", dev->devno);
		return 0;
	}

	spin_lock_irqsave(&dev->slock, flags);
	dev->dev_mask &= ~DEV_EXCLUSIVE;
	spin_unlock_irqrestore(&dev->slock, flags);

	return 0;
}

static ssize_t xmassfd_read(struct file *filp, char __user *buf, size_t count,
                           loff_t *f_pos)
{
    return -EINVAL;
}

static ssize_t xmassfd_write(struct file *filp, const char __user *buf, size_t count,
                            loff_t *f_pos)
{
    return -EINVAL;
}


#define MAX_PROC_BLK	128*4096

#define SPI_BUSY_BIT   0x20
#define SPI_TERM_BIT   0x10
#define SPI_GO_BITS    0x28   /* GO + WRITE */

#define SPI_REQ_BIT    0x01   /* request/grant ownership */
#define SPI_GNT_BIT    0x02   /* grant status */

enum {
    ASM28XX_REG_SWITCH = 0xfff, // 0 - USP control registers, 1 - System control registers
    ASM28XX_GPIO0_CTRL = 0x920, // 0 - input; 1 - output
    ASM28XX_GPIO0_OUT  = 0x928,
    ASM28XX_GPIO0_IN   = 0x930,

    ASM28XX_SPI_DATA_REG = 0x700,
    ASM28XX_SPI_CTRL_REG = 0x704,
    ASM28XX_SPI_3WIRE_REG = 0x705,

    ASM28XX_SPI_MISC_REG = 0x711,
    ASM28XX_SPI_GRANT_REG = 0x720,

    ASM28XX_FWID_0     = 0xf0,
};

enum {
    ESPI_CMD_QCFR_0 = 0x0B,
    ESPI_CMD_WREN = 0x06,

    ESPI_CMD_QCPP_0 = 0x02,
    ESPI_CMD_SSE = 0x20,
    ESPI_CMD_SE = 0xD8,
    ESPI_CMD_BE = 0xC7,
    ESPI_CMD_RDSR = 0x05,

    ESPI_CMD_RDID = 0xAB,
    ESPI_CMD_RDID_0 = 0x9E,
    ESPI_CMD_RDID_1 = 0x9F,
};

int asm28xx_spi_idle(struct xmass_dev *d)
{
    uint32_t iter;
    uint8_t status;
    int res;

    for (iter = 0; ; iter++) {
        res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, &status);
        if (res)
            return res;

        /* wait until BUSY clears */
        if ((status & SPI_BUSY_BIT) == 0)
            return 0;

        /* timeout: ~1,000,000 clock ticks */
        if (iter > 0xF423F) {
            printk(KERN_INFO XPFX "xmass:%d SPI idle timeout! SPI status = 0x%02x\n", d->devno, status);
            return -ETIMEDOUT;
        }
    }

    return 0;
}

int asm28xx_spi_start(struct xmass_dev *d)
{
    uint8_t ctrl;
    int res, j;

    res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, &ctrl);
    if (res)
        return res;

    ctrl &= ~SPI_TERM_BIT;   /* clear terminate / CS bit */

    return pci_write_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, ctrl);
}

int asm28xx_spi_terminate(struct xmass_dev *d)
{
    uint8_t ctrl;

    int res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, &ctrl);
    if (res)
        return res;

    ctrl |= SPI_TERM_BIT;    /* set terminate / CS bit */

    return pci_write_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, ctrl);
}

int asm28xx_spi_write(struct xmass_dev *d, const uint8_t *buf, uint8_t len)
{
    uint32_t value = 0;
    uint8_t ctrl;
    uint8_t i;
    int res;

    /* only 1..4 bytes supported */
    if (len < 1 || len > 4)
        return -EINVAL;

    /* controller must be idle */
    res = asm28xx_spi_idle(d);
    if (res)
        return res;

    /* pack bytes little-endian into a 32-bit word */
    for (i = 0; i < len; i++)
        ((uint8_t *)&value)[i] = buf[i];

    /* write data */
    res = pci_write_config_dword(d->pasmdev, ASM28XX_SPI_DATA_REG, value);
    if (res)
        return res;

    /* start write transaction */
    res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, &ctrl);
    if (res)
        return res;

    // ctrl &= 0x04;
    ctrl = (len & 0x07) | SPI_GO_BITS;

    res = pci_write_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, ctrl);
    if (res)
        return res;

    /* wait for completion */
    return asm28xx_spi_idle(d);
}

int asm28xx_spi_read(struct xmass_dev *d, uint8_t *buf, uint8_t len)
{
    uint32_t value;
    uint8_t ctrl;
    uint8_t i;
    int res;

    /* only 1..4 bytes supported */
    if (len < 1 || len > 4)
        return -EINVAL;

    /* controller must be idle */
    res = asm28xx_spi_idle(d);
    if (res)
        return res;

    /*
     * Program control register:
     *  - low 3 bits = length
     *  - bit 0x20   = start read transaction
     */
    res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, &ctrl);
    if (res)
        return res;

    ctrl = (len & 0x07) | SPI_BUSY_BIT;

    res = pci_write_config_byte(d->pasmdev, ASM28XX_SPI_CTRL_REG, ctrl);
    if (res)
        return res;

    /* wait for read completion */
    res = asm28xx_spi_idle(d);
    if (res)
        return res;

    /* read data */
    res = pci_read_config_dword(d->pasmdev, ASM28XX_SPI_DATA_REG, &value);
    if (res)
        return res;

    /* unpack little-endian bytes */
    for (i = 0; i < len; i++)
        buf[i] = ((uint8_t *)&value)[i];

    return 0;
}

int asm28xx_spi_release_grant(struct xmass_dev *d)
{
    uint8_t val;
    int res;

    res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_GRANT_REG, &val);
    if (res)
        return res;

    val &= ~SPI_REQ_BIT;

    return pci_write_config_byte(d->pasmdev, ASM28XX_SPI_GRANT_REG, val);
}


int asm28xx_spi_get_grant(struct xmass_dev *d)
{
    uint32_t iter;
    uint8_t status;
    int res;

    /* request ownership */
    res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_GRANT_REG, &status);
    if (res)
        return res;

    status |= SPI_REQ_BIT;

    res = pci_write_config_byte(d->pasmdev, ASM28XX_SPI_GRANT_REG, status);
    if (res)
        return res;

    for (iter = 0; ; iter++) {
        res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_GRANT_REG, &status);
        if (res)
            return res;

        /* wait until grant bit is set */
        if (status & SPI_GNT_BIT)
            return 0;

        /* timeout (~3,000,000 ticks) */
        if (iter > 0x2DC6BF) {
            //puts("SPI grant timeout");
            //printf("SPI grant status = 0x%02x\n", status);
            printk(KERN_INFO XPFX "xmass:%d SPI grant timeout! GRANT status = 0x%02x\n", d->devno, status);

            asm28xx_spi_release_grant(d);
            return -ETIMEDOUT;
        }
    }
}

int asm28xx_spi_controller_init(struct xmass_dev *d)
{
    uint8_t val;
    int res;

    /* configure control register */
    res = pci_read_config_byte(d->pasmdev, ASM28XX_SPI_MISC_REG, &val);
    if (res)
        return res;

    val &= 0xD1;

    return pci_write_config_byte(d->pasmdev, ASM28XX_SPI_MISC_REG, val);
}


static int xmass_gpio_stream(struct xmass_dev *d, unsigned delay_ns, unsigned len, uint8_t* s_out, uint8_t* s_in)
{
    unsigned i;
    int res = 0;

    for (i = 0; i < len; i++) {
	res = pci_write_config_byte(d->pasmdev, ASM28XX_GPIO0_OUT, s_out[i]);
	if (res)
		return res;

    if (s_in) {
	res = pci_read_config_byte(d->pasmdev, ASM28XX_GPIO0_IN, &s_in[i]);
	if (res)
		return res;
    }

	if (delay_ns)
		ndelay(delay_ns);
    }

    return res;
}

// Read 1-4 bytes flash status registers registrer
int asm28xx_spi_flash_read_reg(struct xmass_dev *d, uint8_t* cmd, unsigned wrlen, unsigned rdlen, uint8_t *id)
{
    int res;
    res = asm28xx_spi_start(d);
    if (res)
        return res;

    if (wrlen > 0) {
        /* write opcode + dummy bytes */
        res = asm28xx_spi_write(d, cmd, wrlen);
        if (res)
            return res;
    }

    if (rdlen > 0) {
        /* read 3-byte JEDEC / device ID */
        res = asm28xx_spi_read(d, id, rdlen);
        if (res)
            return res;
    }

    res = asm28xx_spi_terminate(d);
    if (res)
        return res;

    return 0;
}

#define SPI_CMD_WRSR        0x01

#define FLASH_READ_CMD  0x03
#define FLASH_WREN_CMD  0x06
#define FLASH_SE_4K_CMD 0x20
#define FLASH_RDSR_CMD  0x05
#define FLASH_PP_CMD    0x02

#define SPI_SR_WIP          0x01    /* Write In Progress */
#define SPI_SR_WEL          0x02    /* Write Enable Latch */

int asm28xx_spi_flash_read(struct xmass_dev *d, unsigned size, uint8_t *out)
{
    uint32_t addr = 0;
    uint8_t cmd[4];
    int res;

    while (addr < size) {
        /* build READ command: 0x03 + 24-bit address */
        cmd[0] = FLASH_READ_CMD;
        cmd[1] = (addr >> 16) & 0xFF;
        cmd[2] = (addr >> 8)  & 0xFF;
        cmd[3] = (addr >> 0)  & 0xFF;
        res = asm28xx_spi_flash_read_reg(d, cmd, 4, 4, &out[addr]);
        if (res)
            return res;

        addr += 4;
    }

    return 1;
}

int asm28xx_spi_write_enable(struct xmass_dev *d)
{
    int res, cnt;
    uint8_t cmd[1];
    uint8_t sr;

    for (cnt = 0; cnt < 1000; cnt++) {
        /* Write Enable */
        cmd[0] = FLASH_WREN_CMD;
        res = asm28xx_spi_flash_read_reg(d, cmd, 1, 0, NULL);
        if (res)
            return res;

        cmd[0] = FLASH_RDSR_CMD;
        res = asm28xx_spi_flash_read_reg(d, cmd, 1, 1, &sr);
        if (res)
            return res;

        if ((sr & SPI_SR_WEL) == SPI_SR_WEL)
            return 0;

        udelay(10);
    }

    dev_info(&d->pasmdev->dev, "WREN TIMEDOUT RDSR = 0x%02x\n", sr);
    return -ETIMEDOUT;
}

int asm28xx_spi_wait_done(struct xmass_dev *d)
{
    int res, cnt;
    uint8_t cmd[1];
    uint8_t sr;

    /* Wait until flash is ready (WIP=0) */
    cmd[0] = FLASH_RDSR_CMD;
    for (cnt = 0; cnt < 1000; cnt++) {
        res = asm28xx_spi_flash_read_reg(d, cmd, 1, 1, &sr);
        if (res)
            return res;

        if ((sr & SPI_SR_WIP) == 0)
            return 0;

        udelay(1000);
    }

    dev_info(&d->pasmdev->dev, "WIP TIMEOUT RDSR = 0x%02x\n", sr);
    return -ETIMEDOUT;
}

int asm28xx_spi_flash_blank(struct xmass_dev *d, unsigned size)
{
    uint32_t addr;
    uint8_t cmd[4];
    uint8_t sr;
    int res, cnt;

    res = asm28xx_spi_write_enable(d);
    if (res)
        return res;

    /* Unprotect */
    cmd[0] = SPI_CMD_WRSR;
    cmd[1] = 0;
    res = asm28xx_spi_flash_read_reg(d, cmd, 2, 0, NULL);
    if (res)
        return res;

    for (addr = 0; addr < size; addr += 0x1000) {
        res = asm28xx_spi_write_enable(d);
        if (res)
            return res;

        /* 4KB Sector Erase (0x20) */
        cmd[0] = FLASH_SE_4K_CMD;
        cmd[1] = (addr >> 16) & 0xFF;
        cmd[2] = (addr >> 8)  & 0xFF;
        cmd[3] = (addr >> 0)  & 0xFF;
        res = asm28xx_spi_flash_read_reg(d, cmd, 4, 0, NULL);
        if (res)
            return res;

        /* Wait until flash is ready (WIP=0) */
        res = asm28xx_spi_wait_done(d);
        if (res)
            return res;
    }

    return 0;
}

int asm28xx_spi_flash_write_page(struct xmass_dev *d, uint32_t addr,
                                 const uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];
    unsigned off;
    int res, cnt;
    cmd[0] = FLASH_PP_CMD;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8)  & 0xFF;
    cmd[3] = (addr >> 0)  & 0xFF;

    res = asm28xx_spi_write_enable(d);
    if (res)
        return res;

    res = asm28xx_spi_start(d);
    if (res)
        return res;

    res = asm28xx_spi_write(d, cmd, 4);
    if (res)
        return res;

    for (off = 0; off < len; off += 4) {
        res = asm28xx_spi_write(d, data + off, 4);
        if (res)
            return res;
    }

    res = asm28xx_spi_terminate(d);
    if (res)
        return res;

    return asm28xx_spi_wait_done(d);
}

int asm28xx_spi_flash_write(struct xmass_dev *d, const uint8_t *image, uint32_t total)
{
    uint32_t offset;
    int res;

    /* program page-by-page (256 bytes typical) */
    for (offset = 0; offset < total; offset += 256) {
        res = asm28xx_spi_flash_write_page(d, offset, image + offset, 256);
        if (res)
            return res;
    }

    return 0;
}

void asm28xx_signoff(uint8_t *tmp, const char *payload)
{
    uint8_t sum = 0;
    unsigned len = strlen(payload), i;
    const unsigned max_len = 0xE30 - 0xE10;
    static const uint8_t g_asm_id[16] = {
        0x88, 0xb0, 0x15, 0x9a, 0x5e, 0xe3, 0xae, 0x40,
        0xaf, 0x8f, 0x00, 0x49, 0x58, 0xba, 0x7a, 0xf2
    };

    /* 1) place id at 0xE00 */
    memcpy(tmp + 0xE00, g_asm_id, 16);

    /* 2) place custom string at 0xE10..0xE30 (no NULL terminator) */
    if (len > max_len)
        len = max_len;

    memcpy(tmp + 0xE10, payload, len);

    /* 3) marker byte */
    tmp[0xEFE] = 0x5A;

    /* 4) checksum over [0xE00 .. 0xEFE-1] */
    for (i = 0xE00; i < 0xEFE; i++)
        sum += tmp[i];

    tmp[0xEFF] = sum;
}


#define ASM28XX_FLASH_IMAGE_SIZE  65536
uint8_t s_temp_data[ASM28XX_FLASH_IMAGE_SIZE];

static long xmassfd_ioctl(struct file *filp,
			 unsigned int ioctl_num,/* The number of the ioctl */
			 unsigned long ioctl_param) /* The parameter to it */
{
	int res;
	struct xmass_dev *xmassdev = filp->private_data;
	void __user *uptr = (void __user *)ioctl_param;

	if (!(xmassdev->dev_mask & DEV_VALID))
		return -EIO;

	if (xmassdev->pasmdev->error_state != pci_channel_io_normal) {
		dev_err(&xmassdev->pasmdev->dev, "PCI device is disconnected or in error state\n");
		return -EIO;
	}

	// JTAG bit-banging transfer
	switch (ioctl_num) {
    case PCIE_FLASH_WRITE: {
        if (copy_from_user(s_temp_data, uptr, ASM28XX_FLASH_IMAGE_SIZE))
            return -EFAULT;

        asm28xx_signoff(s_temp_data, "XMASS");

        res = pm_runtime_resume_and_get(&xmassdev->pasmdev->dev);
		if (res < 0)
			return res;

        res = res ? res : pci_write_config_byte(xmassdev->pasmdev, ASM28XX_REG_SWITCH, 1); // Switch to GPIO control mode
        res = res ? res : asm28xx_spi_controller_init(xmassdev);
        res = res ? res : asm28xx_spi_get_grant(xmassdev);

        res = res ? res : asm28xx_spi_flash_write(xmassdev, s_temp_data, ASM28XX_FLASH_IMAGE_SIZE);

        asm28xx_spi_release_grant(xmassdev);
        pm_runtime_put(&xmassdev->pasmdev->dev);
        return res;
    }
    case PCIE_FLASH_ERASE: {
		res = pm_runtime_resume_and_get(&xmassdev->pasmdev->dev);
		if (res < 0)
			return res;

        res = res ? res : pci_write_config_byte(xmassdev->pasmdev, ASM28XX_REG_SWITCH, 1); // Switch to GPIO control mode
        res = res ? res : asm28xx_spi_controller_init(xmassdev);
        res = res ? res : asm28xx_spi_get_grant(xmassdev);
        res = res ? res : asm28xx_spi_flash_blank(xmassdev, ASM28XX_FLASH_IMAGE_SIZE);

        asm28xx_spi_release_grant(xmassdev);
        pm_runtime_put(&xmassdev->pasmdev->dev);
        return res;
    }
    case PCIE_FLASH_READ: {
		res = pm_runtime_resume_and_get(&xmassdev->pasmdev->dev);
		if (res < 0)
			return res;

        res = res ? res : pci_write_config_byte(xmassdev->pasmdev, ASM28XX_REG_SWITCH, 1); // Switch to GPIO control mode
        res = res ? res : asm28xx_spi_controller_init(xmassdev);
        res = res ? res : asm28xx_spi_get_grant(xmassdev);
        res = res ? res : asm28xx_spi_flash_read(xmassdev, ASM28XX_FLASH_IMAGE_SIZE, s_temp_data);

        asm28xx_spi_release_grant(xmassdev);
        pm_runtime_put(&xmassdev->pasmdev->dev);

        if (copy_to_user(uptr, s_temp_data, ASM28XX_FLASH_IMAGE_SIZE))
            return -EFAULT;

        return res;
    }
    case PCIE_GETIDS: {
        uint8_t fwid[6] = { 0, };
        uint8_t rdid[4] = { 0, };
        uint8_t cmd = ESPI_CMD_RDID_1;
        unsigned j;

		res = pm_runtime_resume_and_get(&xmassdev->pasmdev->dev);
		if (res < 0)
			return res;

        for (j = 0; j < 6; j++) {
            res = res ? res : pci_read_config_byte(xmassdev->pasmdev, ASM28XX_FWID_0 + j, &fwid[j]);
        }

        res = res ? res : pci_write_config_byte(xmassdev->pasmdev, ASM28XX_REG_SWITCH, 1); // Switch to GPIO control mode
        res = res ? res : asm28xx_spi_controller_init(xmassdev);
        res = res ? res : asm28xx_spi_get_grant(xmassdev);
        res = res ? res : asm28xx_spi_flash_read_reg(xmassdev, &cmd, 1, 4, rdid);

        asm28xx_spi_release_grant(xmassdev);
        pm_runtime_put(&xmassdev->pasmdev->dev);

        if (copy_to_user(uptr, fwid, 6))
            return -EFAULT;

        if (copy_to_user(uptr + 6, rdid, 4))
            return -EFAULT;

        return res;
    }
	case PCIE_GPIOS_8: {
		unsigned len;
		unsigned delay_ns;
		struct xmass_iop iop;
		int has_rx;

		static uint8_t out_data[MAX_PROC_BLK];
		static uint8_t in_data[MAX_PROC_BLK];

		if (copy_from_user(&iop, uptr, sizeof(iop)))
			return -EFAULT;

		len = iop.io_len;
		if (len > MAX_PROC_BLK)
			len = MAX_PROC_BLK;

		delay_ns = iop.delay_ns;
		has_rx = (iop.in_buf != NULL);

		if (copy_from_user( out_data, iop.out_buf, len))
			return -EFAULT;

		/* Ensure ASM2806 isn't in sleep mode */
		res = pm_runtime_resume_and_get(&xmassdev->pasmdev->dev);
		if (res < 0)
			return res;

		printk(KERN_NOTICE XPFX "XMASS_JTAG_IO LEN=%d (%d) NS=%d\n", len, iop.io_len, delay_ns);

		pci_write_config_byte(xmassdev->pasmdev, ASM28XX_REG_SWITCH, 1); // Switch to GPIO control mode
		pci_write_config_byte(xmassdev->pasmdev, ASM28XX_GPIO0_CTRL, (1 << 0) | (1 << 3) | (1 << 5));

		res = xmass_gpio_stream(xmassdev, delay_ns, len, out_data, has_rx ? in_data : NULL);

		pm_runtime_put(&xmassdev->pasmdev->dev);

		if (res)
			return res;

		if (has_rx) {
			if (copy_to_user(iop.in_buf, in_data, len))
				return -EFAULT;
		}

		return len;
	}
	}

	return -EINVAL;
}


struct file_operations xmass_fops = {
        .owner =   THIS_MODULE,
        .unlocked_ioctl = xmassfd_ioctl,
        .open =    xmassfd_open,
        .release = xmassfd_release,
        .read =    xmassfd_read,
        .write =   xmassfd_write,
};

static int xmass_setup_cdev(struct xmass_dev *xmassdev)
{
        dev_t dev_num = xmass_dev_first + xmassdev->devno;

        cdev_init(&xmassdev->cdev, &xmass_fops);
        xmassdev->cdev.owner = THIS_MODULE;
        xmassdev->cdev.ops = &xmass_fops;
        return cdev_add (&xmassdev->cdev, dev_num, 1);
}


static int xmass_probe(struct pci_dev *pdev)
{
	struct xmass_dev* xmassdev;
    unsigned xmass_no = find_first_zero_bit(xmass_busy_map, TOT_XMASS_DEVS);
	int err;

    if (xmass_no == TOT_XMASS_DEVS) {
        dev_err(&pdev->dev, "XMASS: Maximum number of devices is reahced, ignoring device!\n");
        return -ENOMEM;
    }

	xmassdev = kzalloc(sizeof(*xmassdev), GFP_KERNEL);
	if (!xmassdev) {
		dev_err(&pdev->dev, "Failed to allocate memory.\n");
		return -ENOMEM;
	}

	xmassdev->devno = xmass_no;
	xmassdev->pasmdev = pci_dev_get(pdev);
	xmassdev->dev_mask = 0;

	spin_lock_init(&xmassdev->slock);

	xmassdev->dev_mask = DEV_VALID;
	xmassdev->cdevice = device_create(xmass_class,
					 &pdev->dev,
					 MKDEV(MAJOR(xmass_dev_first), MINOR(xmass_dev_first) + xmassdev->devno),
					 NULL,
					 XMASS_DEVICE_NAME "%d",
                     xmassdev->devno);
	if (IS_ERR(xmassdev->cdevice)) {
		printk(KERN_NOTICE XPFX "Unable to register device class\n");
		goto failed_device;
	}

	err = xmass_setup_cdev(xmassdev);
	if (err) {
		printk(KERN_NOTICE XPFX "Error %d initializing cdev\n", err);
		goto failed_cdev;
	}

    dev_info(&pdev->dev, "is linked to /dev/xmass%d\n", xmass_no);

    set_bit(xmass_no, xmass_busy_map);
	xmassdev->asm_bus_number = pdev->bus->number;
	xmassdev->next = xmass_list;
	xmass_list = xmassdev;
	return 0;

failed_cdev:
    device_destroy(xmass_class, MKDEV(MAJOR(xmass_dev_first), MINOR(xmass_dev_first) + xmassdev->devno));
failed_device:
	pci_dev_put(pdev);
	kfree(xmassdev);
	return err;
}


static void xmass_dev_remove(struct xmass_dev* xmassdev)
{
	dev_info(&xmassdev->pasmdev->dev, "is unlinked, /dev/xmass%d removed\n", xmassdev->devno);

	cdev_del(&xmassdev->cdev);
	//struct device* p = get_device(&xmassdev->pasmdev->dev);
	//if (p) {
		device_destroy(xmass_class, MKDEV(MAJOR(xmass_dev_first), MINOR(xmass_dev_first) + xmassdev->devno));
	//	put_device(&xmassdev->pasmdev->dev);
	//} else {
	//	dev_info(&xmassdev->pasmdev->dev, "device is empty\n");
	//}
	pci_dev_put(xmassdev->pasmdev);

	xmassdev->dev_mask = 0;

	xmassdev->pasmdev = NULL;
	xmassdev->asm_bus_number = ~0U;

    clear_bit(xmassdev->devno, xmass_busy_map);
}

static struct xmass_dev *get_xmass_by_pci(struct pci_dev *pdev)
{
    struct xmass_dev *xptr = xmass_list;
    while (xptr != NULL) {
	if (xptr->pasmdev == pdev) {
		return xptr;
	}
	xptr = xptr->next;
    }
    return NULL;
}

static void xmass_remove(struct pci_dev *pdev)
{
	struct xmass_dev *xptr = get_xmass_by_pci(pdev);
	if (xptr != NULL) {
		xmass_dev_remove(xptr);
	}
}

enum {
    XMASS_PCI_VID = 0x1B21,
    XMASS_PCI_DID = 0x2806,
};

MODULE_DEVICE_TABLE(pci, usdr_pci_table);

static struct pci_driver usdr_driver = {
	.name		= DRV_NAME,
        .id_table	= usdr_pci_table,
        .probe		= usdr_probe,
        .remove	= usdr_remove
};

static int asm28xx_get_fw(struct pci_dev *pdev, uint8_t *fwid)
{
    int i, res;
    for (i = 0; i < 6; i++) {
	res = pci_read_config_byte(pdev, ASM28XX_FWID_0 + i, &fwid[i]);
	if (res)
	    return res;
    }
    return 0;
}

static int check_for_xamss(void)
{
    struct pci_dev *pdev = NULL;
    int res;
    uint8_t fwid[6];

    while ((pdev = pci_get_device(XMASS_PCI_VID, XMASS_PCI_DID, pdev))) {
	if (pci_pcie_type(pdev) != PCI_EXP_TYPE_UPSTREAM)
		continue;

	/* Ensure ASM2806 isn't in sleep mode */
	res = pm_runtime_resume_and_get(&pdev->dev);
	if (res < 0)
		return res;

	res = asm28xx_get_fw(pdev, fwid);
	if (res) {
		dev_err(&pdev->dev, "Unable to read firmware ID, error %x\n", res);

		pm_runtime_put(&pdev->dev);
		continue;
	}

	dev_info(&pdev->dev, "Found XMASS device: [%02x:%04x] ASM2806 Firmware %02x%02x%02x%02x%02x%02x %px\n", pdev->bus->number, pdev->devfn, fwid[0], fwid[1], fwid[2], fwid[3], fwid[4], fwid[5], pdev);
	res = xmass_probe(pdev);
	if (res) {
		dev_info(&pdev->dev, "Unable to create XMASS char dev: %d\n", res);
	}

	/* do NOT pci_dev_put(pdev) here */
	pm_runtime_put(&pdev->dev);
    }

    /* Release the final reference */
    pci_dev_put(pdev);

    return 0;
}

static void release_xmasses(void)
{
    struct xmass_dev *xptr = xmass_list;
    while (xptr != NULL) {
	xmass_dev_remove(xptr);
	xptr = xptr->next;
    }
}

static int __init usdr_init(void)
{
	int err;
    err = alloc_chrdev_region(&usdr_dev_first, 0, TOT_USDR_DEVS, DRV_NAME);
	if (err) {
        printk(KERN_NOTICE PFX "Unable to allocate chrdev region: %d\n", err);
        goto failed_chrdev_usdr;
	}
    err = alloc_chrdev_region(&xmass_dev_first, 0, TOT_XMASS_DEVS, DRV_NAME);
    if (err) {
        printk(KERN_NOTICE XPFX "Unable to allocate chrdev region: %d\n", err);
        goto failed_chrdev_xmass;
    }

#ifndef HAVE_CLASS_CREATE_ONE_ARG
        usdr_class = class_create(THIS_MODULE, CLASS_NAME);
#else
        usdr_class = class_create(CLASS_NAME);
#endif
        if (IS_ERR(usdr_class)) {
                printk(KERN_NOTICE PFX "Unable to register usdr class\n");
		goto failed_setup_cdev;
	}

#ifndef HAVE_CLASS_CREATE_ONE_ARG
        xmass_class = class_create(THIS_MODULE, XMASS_CLASS_NAME);
#else
        xmass_class = class_create(XMASS_CLASS_NAME);
#endif
        if (IS_ERR(usdr_class)) {
                printk(KERN_NOTICE PFX "Unable to register xmass class\n");
		goto failed_xmass;
	}

        err = pci_register_driver(&usdr_driver);
	if (err) {
		printk(KERN_NOTICE PFX "Unable to register PCI driver: %d\n", err);
		goto failed_pci;
	}

	check_for_xamss();
	return 0;

failed_pci:
        class_destroy(xmass_class);
failed_xmass:
        class_destroy(usdr_class);
failed_chrdev_xmass:
    unregister_chrdev_region(xmass_dev_first, TOT_XMASS_DEVS);
failed_setup_cdev:
    unregister_chrdev_region(usdr_dev_first, TOT_USDR_DEVS);
failed_chrdev_usdr:
	return err;
}

static void __exit usdr_cleanup(void)
{
        struct usdr_dev *ptr = usdr_list, *next;
        struct xmass_dev *xptr = xmass_list, *xnext;

        pci_unregister_driver(&usdr_driver);
    // Manually clean
	release_xmasses();

        class_destroy(usdr_class);
        class_destroy(xmass_class);

    unregister_chrdev_region(usdr_dev_first, TOT_USDR_DEVS);
    unregister_chrdev_region(xmass_dev_first, TOT_XMASS_DEVS);

	while (ptr != NULL) {
		next = ptr->next;
		kfree(ptr);
		ptr = next;
	}

	while (xptr != NULL) {
		xnext = xptr->next;
		kfree(xptr);
		xptr = xnext;
	}
}


module_init(usdr_init);
module_exit(usdr_cleanup);



