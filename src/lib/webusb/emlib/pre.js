// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

var usb_device;

function set_usb_device(dev) {
    usb_device = dev;
    return 0;
}

async function write_ep1(device, data, len) {
    const datau = new Uint8Array(wasmMemory.buffer, ((data)>>0), len);
    await usb_device.transferOut(1, datau);
    console.log(`write_ep1 ${device}, ${data}, ${len}:` + datau)
    return len;
}

async function write_ep2(device, data, len) {
    const datau = new Uint8Array(wasmMemory.buffer, ((data)>>0), len);
    await usb_device.transferOut(2, datau);
    console.log(`write_ep2 ${device}, ${data}, ${len}:` + datau)
    return len;
}

async function read_ep1(device, data, len) {
    console.log(`read_ep1 ${device}, ${data}, ${len}`)
    const result = await usb_device.transferIn(1, 512);

    if (result.status == "ok") {
	readbackvalue = new Uint8Array(result.data.buffer);
	writeArrayToMemory(readbackvalue, data);
	
	console.log(` => rb1 ${readbackvalue}`);
	return readbackvalue.length;
    } else {
	return -2222;
    }
}

async function read_ep2(device, data, len) {
    console.log(`read_ep2 ${device}, ${data}, ${len} `)
    const result = await usb_device.transferIn(2, 64);

    if (result.status == "ok") {
	readbackvalue = new Uint8Array(result.data.buffer);
	writeArrayToMemory(readbackvalue, data);

	console.log(` => rb2 ${readbackvalue}`);
	return readbackvalue.length;
    } else {
	return -2222;
    }
}

async function write_log_js(device, sevirity, str) {
    var s = AsciiToString(str);
    console.log(`write_log ${device}, ${sevirity}, ${s} `)
    return 0;
}

