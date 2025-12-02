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
#include <asm/page.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/ktime.h>

#include "./pcie_uram_driver_if.h"
#include "./si2c.c"
#include "./spiext.h"
#include "./device_cores.h"

#define DRV_NAME		"usdr"
#define PFX			DRV_NAME ": "

#define DEVICE_NAME		DRV_NAME
#define CLASS_NAME		DRV_NAME

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

static struct usdr_dev *usdr_list = NULL;
static int devices = 0;
static dev_t dev_first;
static struct class* usdr_class  = NULL;

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
                ioctl_num != PCIE_DRIVER_CLAIM_VERSION) {

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
        dev_t dev_num = dev_first + usdrdev->devno;

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
        unsigned usdr_no = devices;
	size_t bar_len;

	printk(KERN_INFO PFX "Initializing %s\n", pci_name(pdev));
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, "
			"aborting.\n");
		return err;
	}

	pci_set_master(pdev);

	/* Reconfigure MaxReadReq to 4KB */
	pcie_capability_clear_and_set_word(pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_READRQ, PCI_EXP_DEVCTL_READRQ_1024B);

    //dma_set_mask_and_coherent
	if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		dev_err(&pdev->dev,"No suitable consistent DMA available.\n");
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
					 MKDEV(MAJOR(dev_first), MINOR(dev_first) + devices),
					 NULL,
					 DEVICE_NAME "%d",
					 devices);
        if (IS_ERR(usdrdev->cdevice)) {
		printk(KERN_NOTICE PFX "Unable to register device class\n");
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
    if (err)
    {
        printk(KERN_NOTICE PFX "Error %d initializing bucket\n", err);
        deinit_bucket(usdrdev);
        goto failed_cdev;
    }


	devices++;
        usdrdev->next = usdr_list;
        usdr_list = usdrdev;
	return 0;

        //cdev_del(&usdrdev->cdev);
failed_cdev:
        device_destroy(usdr_class, MKDEV(MAJOR(dev_first), MINOR(dev_first) + devices));
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
};


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
        device_destroy(usdr_class, MKDEV(MAJOR(dev_first), MINOR(usdrdev->devno)));
	
        usdrdev->dev_mask = 0;

        pci_iounmap(pdev, usdrdev->bar_addr);
	pci_release_regions(pdev);

	pci_clear_master(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	

        //usdr_freedma(usdrdev, usdrdev->rxdma, usdrdev->rxdma_bufsize);

	devices--;
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

MODULE_DEVICE_TABLE(pci, usdr_pci_table);

static struct pci_driver usdr_driver = {
	.name		= DRV_NAME,
        .id_table	= usdr_pci_table,
        .probe		= usdr_probe,
        .remove	= usdr_remove
};


static int __init usdr_init(void)
{
	int err;
	err = alloc_chrdev_region(&dev_first, 0, 32, DRV_NAME);
	if (err) {
		printk(KERN_NOTICE PFX "Unable to allocate chrdev region: %d\n", err);
		goto failed_chrdev;
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

        err = pci_register_driver(&usdr_driver);
	if (err) {
		printk(KERN_NOTICE PFX "Unable to register PCI driver: %d\n", err);
		goto failed_pci;
	}
	return 0;

failed_pci:
        class_destroy(usdr_class);
failed_setup_cdev:
	unregister_chrdev_region(dev_first, devices);
failed_chrdev:
	return err;
}

static void __exit usdr_cleanup(void)
{
        struct usdr_dev *ptr = usdr_list, *next;

        pci_unregister_driver(&usdr_driver);

        class_destroy(usdr_class);

	unregister_chrdev_region(dev_first, devices);

	while (ptr != NULL) {
		next = ptr->next;
		kfree(ptr);
		ptr = next;
	}
}


module_init(usdr_init);
module_exit(usdr_cleanup);



