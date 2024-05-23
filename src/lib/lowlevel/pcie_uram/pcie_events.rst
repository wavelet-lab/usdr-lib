Interrupt event handling
========================

Event is generated in verilog.
Each event read associated data defined in ``A_EVENT_ADDRS/A_EVENT_LEN`` and sent it to host right before generating interrupt

 ::

	event_fifo_gen #(
	    //                    none      i2c     spi3     spi2     spi1     spi0     tx0     rx0
	    .A_EVENT_ADDRS(256'h00000000_00000001_0000000E_0000000D_00000003_00000002_0000001C_00000004),
	    .A_EVENT_LEN(  256'h00000000_00000000_00000000_00000000_00000000_00000000_00000002_00000002)
	) event_fifo_gen (
	    ...
	);


Example messages received in driver

::

	Byte_3   Byte_2   Byte_1   Byte_0
	ed2eee6d_fe000000_baadbeef_06d10006
	ee2fef6e_fe000000_baadbeef_06d20006
	ef30f06f_fe000000_baadbeef_06d30006
	f031f170_fe000000_beef0195_06d40006
	f132f271_fe000000_01951c60_06d50006
	cf10d04f_fe000000_80000000_06732000
	ADDR_2   ADDR_1   ADDR_0   metadata


Metadata used to identify actual interrupt

``[5:0]   -- Event number``

``[11:6]  -- Aux data (not used)``

``[15:12] -- Event size (not used)``

``[31:16] -- Sequence number (used to detect overflow)`` 


Then this event data is stored to event specific storage

``d->streaming[event_no].stat_data[k + 0] = data[1];   // DATA[ADDR_0]``

``d->streaming[event_no].stat_data[k + 1] = data[2];   // DATA[ADDR_1]``

``d->streaming[event_no].stat_data[k + 2] = data[3];   // DATA[ADDR_2]``

``d->streaming[event_no].stat_data[k + 3] = timestamp;``

And later with ``usdr_stream_wait_or_alloc()`` call it's filled in OOB storage

::

        if (ooidx < oob_cnt_max) {
            // timestamp in NS, so will wrap every ~4s, which is enough to calculate jitter between calls
            oob_out_u64[2 * ooidx + 0] = (((uint64_t)stat_data[1]) << 32) | stat_data[0];
            oob_out_u64[2 * ooidx + 1] = (((uint64_t)timestamp) << 32) | stat_data[2];
            oobcnt++;




  
    