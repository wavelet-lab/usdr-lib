// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

'use strict'

const usb = require("webusb").usb;
const glue = require("./build/control.js");


// console.dir(glue)
// int send_command(int fd, const char* cmd, size_t cmd_len, char* res, size_t res_len)
async function device_call(device, request) {
    var in_cmd = glue._malloc(512);
    var out_res = glue._malloc(512);
    glue.stringToAscii(request, in_cmd);
    //var res = await glue._send_command(0, in_cmd, request.length, out_res, 512);
    var res = await glue.ccall("send_command", "number",
	["number", "number", "number", "number", "number"],
	[0, in_cmd, request.length, out_res, 512], {async: true});
    var res_str;


    if (res != 0) {
	res_str = `{ "error": ${res} }`;
    } else {
	res_str = glue.AsciiToString(out_res);
    }

    glue._free(in_cmd);
    glue._free(out_res);

    console.log(`send_command("${request}") => ${res} : ${res_str}`);
    return res_str;
    //return JSON.parse(res_str);
}

var sig_stop = false;
async function submit_rxpacket_usdr(device, pktsz) {
    var bsz = (pktsz * 4 + 511) & 0xfffe00; //Should be rounded up to 512b boundary
    bsz += 512; //Extra buffer for packet metadata
    device.transferIn(3, bsz).then( (res) => {
	//if (sig_stop) {
	//    console.log(`STOP`);
	//} else
	 if (res.status == "ok") {
	    var data = new Uint8Array(res.data.buffer)
	    console.log(`got usdr packet ${data.length} bytes`)

	    submit_rxpacket_usdr(device, pktsz);
	} else {
	    console.log(`ERROR: ${res.status}`);
	}
    });
}

async function submit_rxpacket_lime(device, pktsz) {
    device.transferIn(3, pktsz).then( (res) => {
	if (sig_stop) {
	    console.log(`STOP`);
	} else
	 if (res.status == "ok") {
	    var data = new Uint8Array(res.data.buffer)
	    console.log(`got lime packet ${data.length} bytes`)

	    submit_rxpacket_lime(device, pktsz);
	} else {
	    console.log(`ERROR: ${res.status}`);
	}
    });
}



var time = 10000;
var tx = 0;
async function submit_txpacket(device) {
    var buffer = new Uint8Array(4096 + 16);
    buffer[0] = (time >> 0) & 0xff;
    buffer[1] = (time >> 8) & 0xff;
    buffer[2] = (time >> 16) & 0xff;
    buffer[3] = (time >> 24) & 0xff;

    buffer[4] = 0;
    buffer[5] = 0;
    buffer[6] = 0xff;
    buffer[7] = 0x03;


    time += 4096 / 8;

    device.transferOut(3, buffer).then( (res) => {
	if (res.status == "ok") {
	    console.log(`sent ${tx} packet packet`)
	    tx++;
	    if (tx < 32000 && !sig_stop)
		submit_txpacket(device);
	} else {
	    console.log(`ERROR: ${res.status}`);
	}
    }).catch( (res) => {
	console.log(`CATCH: ${res}`);
    });;
}

async function dump_dev(device) {
    await device_call(device, '{"req_method":"sdr_debug_dump"}');
}


async function main() {
    var lime = false;

    const device = await usb.requestDevice({
//    filters: [{vendorId: 0xfaef}]
//    filters: [{vendorId: lime ? 0x0403 : 0x3727}]
    filters: [{vendorId: lime ? 0x0403 : 0xfaee}]

    });

    await device.open();


    if (lime) await device.reset();
    await device.selectConfiguration(1);
    await device.claimInterface(0);
    if (lime) await device.claimInterface(1);


    // console.dir(device);
    console.log(`== Opening control library with ${device.productId} ==`);
    //glue.calledMain = device;
    glue.set_usb_device(device);

    console.log(`== calling init_lib(): device=${device} vid=${device.vendorId} pid=${device.productId} ==`);

    var fd = await glue.ccall("init_lib", "number",
	["number", "number", "number"],
	[device, device.vendorId, device.productId], {async: true});

    console.log(`== init_lib result: ${fd} ==`);

    console.log("=================================================================================");
    //Set frequency
    var ret = await device_call(device,
     '{"req_method":"sdr_set_rx_frequency","req_params":{"chans":1,"frequency":100000000}}'
    );
    console.log(ret);


    console.log("=================================================================================");
    //Start streaming
    var ret = await device_call(device,
     '{"req_method":"sdr_init_streaming","req_params":{"chans":1,"samplerate":2500000,"packetsize":8160}}'
    );
    console.log(ret);


    console.log("=================================================================================");


    if (lime)
        submit_rxpacket_lime(device, 8192);
    else
        submit_rxpacket_usdr(device, 8160);
//    submit_txpacket(device);


    var j = 0;
    for (j = 0; j < 3; j++) {
	await new Promise(r => setTimeout(r, 500));
	//await dump_dev(device);
	const start = Date.now();
	await await device_call(device,
     '{"req_method":"sdr_ctrl_streaming","req_params":{"samplerate":21500000, "param": 42, "throttleon":8000000}}'
	);
	const end = Date.now();
	console.log(`Execution time: ${end - start} ms`);

    }


    console.log("=================================================================================");
    var ret = await device_call(device,
     '{"req_method":"sdr_stop_streaming"}'
    );
    console.log(ret);

    console.log("=================================================================================");

    sig_stop = true;
    await new Promise(r => setTimeout(r, 1000));

    // await device.clearHalt('in', 3);


    await device.close();
}

glue.onRuntimeInitialized = (async() => {
    console.log("== Starting main");
    await main();
});

glue.run()

//if (require.main === module) {
//    (async() => {
//	await main();
//    })().catch( e => {
//	console.log(`Catch error: ${e}`);
//    } );
//}
