#!/usr/bin/python

import subprocess
import time
import signal

class wvltSDR:

    USDR_DM_CREATE_PATH = '/home/irod/code/usdr-host/usdr-lib/build-src-Desktop-Debug/tools/usdr_dm_create'
    OPTS = {
        '-D' : [None, None],            #  device_parameters] \n"
        '-f' : [None, './out.data'],    #  RX_filename [./out.data]] \n"
        '-I' : [None, None],            #  TX_filename(s) (optionally colon-separated list)] \n"
        '-o' : [None, None],            #  <flag: cycle TX from file>] \n"
        '-c' : [None, 128],             #  count [128]] \n"
        '-r' : [None, int(50E+6)],      #  samplerate [50e6]] \n"
        '-F' : [None, 'ci16'],          #  format [ci16] | cf32] \n"
        '-C' : [None, None],            #  chmsk [autodetect]] \n"
        '-S' : [None, 4096],            #  TX samples_per_blk [4096]] \n"
        '-O' : [None, 4096],            #  RX samples_per_blk [4096]] \n"
        '-t' : [None, None],            #  <flag: TX only mode>] \n"
        '-T' : [None, None],            #  <flag: TX+RX mode>] \n"
        '-N' : [None, None],            #  <flag: No TX timestamps>] \n"
        '-q' : [None, int(910E+6)],     #  TDD_FREQ [910e6]] \n"
        '-e' : [None, int(900E+6)],     #  RX_FREQ [900e6]] \n"
        '-E' : [None, int(920E+6)],     #  TX_FREQ [920e6]] \n"
        '-w' : [None, int(1E+6)],       #  RX_BANDWIDTH [1e6]] \n"
        '-W' : [None, int(1E+6)],       #  TX_BANDWIDTH [1e6]] \n"
        '-y' : [None, 15],              #  RX_GAIN_LNA [15]] \n"
        '-Y' : [None, 0],               #  TX_GAIN [0]] \n"
        '-p' : [None, 'rx_auto'],       #  RX_PATH ([rx_auto]|rxl|rxw|rxh|adc|rxl_lb|rxw_lb|rxh_lb)] \n"
        '-P' : [None, 'tx_auto'],       #  TX_PATH ([tx_auto]|txb1|txb2|txw|txh)] \n"
        '-u' : [None, 15],              #  RX_GAIN_PGA [15]] \n"
        '-U' : [None, 15],              #  RX_GAIN_VGA [15]] \n"
        '-a' : [None, 'internal'],      #  Reference clock path [internal]] \n"
        '-x' : [None, None],            #  Reference clock frequency [internal clock freq]] \n"
        '-B' : [None, 0],               #  Calibration freq [0]] \n"
        '-s' : [None, 'all'],           #  Sync type [all]] \n"
        '-Q' : [None, None],            #  <flag: Discover and exit>] \n"
        '-R' : [None, 0],               #  RX_LML_MODE [0]] \n"
        '-A' : [None, 0],               #  Antenna configuration [0]] \n"
        '-X' : [None, None],            #  <flag: Skip initialization>] \n"
        '-z' : [None, None],            #  <flag: Continue on error>] \n"
        '-l' : [None, 3],               #  loglevel [3(INFO)]] \n"
        '-h' : [None, None]             #  <flag: This help>]",
    }

    def setOptionValue(self, key, value = ''):
        if key in self.OPTS.keys(): self.OPTS[key][0] = value

    def setOptionFlag(self, key): self.setOptionValue(key, '')
    def unsetOptionFlag(self, key): self.setOptionValue(key, None)

    def getOptionValue(self, key): return self.OPTS[key][0]
    def getOptionDefault(self, key): return self.OPTS[key][1]
    def isSetOptionFlag(self, key): return (self.OPTS[key] is not None and self.OPTS[key][0] is not None)

    args = []
    proc = None

    LOGFILE = './wvlt_sdr.log'
    fd = None

    def __init__(self):
        self.fd = open(self.LOGFILE, 'w')

    def __del__(self):
        if self.proc is not None:
            self.stop(force=True)
        self.fd.close()

    def constructArgs(self):
        self.args[:] = []
        self.args.append(self.USDR_DM_CREATE_PATH)

        for opt in self.OPTS.keys():
            optval = self.getOptionValue(opt)
            if optval is None: continue
            self.args.append(opt)
            if optval <> '': self.args.append(str(optval))

    def resetOpts(self):
        for opt in self.OPTS.keys(): self.setOptionValue(opt, None)
        self.constructArgs()

    def setSampleRate(self, rate):      self.setOptionValue('-r', rate);
    def setChannelMask(self, chmask):   self.setOptionValue('-C', chmask)
    def setBlocksCount(self, cnt):      self.setOptionValue('-c', cnt)
    def setMaxBlocksCount(self):   self.setOptionValue('-c', -1)
    def setDataFormat(self, fmt):       self.setOptionValue('-F', fmt)

    def setRXBlockSize(self, samples):  self.setOptionValue('-O', samples)
    def setTXBlockSize(self, samples):  self.setOptionValue('-S', samples)

    def enableRXOnly(self):
        self.unsetOptionFlag('-T')
        self.unsetOptionFlag('-t')

    def enableTXOnly(self):
        self.unsetOptionFlag('-T')
        self.setOptionFlag('-t')

    def enableTXRX(self):
        self.unsetOptionFlag('-t')
        self.setOptionFlag('-T')

    def setRXFreq(self, freq):      self.setOptionValue('-e', freq)
    def setTXFreq(self, freq):      self.setOptionValue('-E', freq)
    def setRXBW(self, bw):          self.setOptionValue('-w', bw)
    def setTXBW(self, bw):          self.setOptionValue('-W', bw)
    def setRXGain(self, gain):      self.setOptionValue('-y', gain)
    def setTXGain(self, gain):      self.setOptionValue('-Y', gain)
    def setRXFileOut(self, fname):  self.setOptionValue('-f', fname)

    def setTXFileIn(self, fname, loop = False):
        self.setOptionValue('-I', fname)
        if loop:
            self.setOptionFlag('-o')
        else:
            self.unsetOptionFlag('-o')

    def start(self):
        self.constructArgs();
        print self.args
        self.proc = subprocess.Popen(args=self.args, stdout=self.fd, stderr=self.fd)
        return self.proc

    def checkState(self):
        if self.proc is None:
            return 0
        else:
            self.proc.poll()
            return self.proc.returncode

    def stop(self, force=False):
        if self.proc is None:
            return True
        if self.checkState() is not None:
            return True

        if force:
            self.proc.kill()
        else:
            self.proc.send_signal(signal.SIGINT)

        self.proc.wait()

        res = self.checkState()
        if res is not None:
            self.proc = None
            return True

        return False


def main():

    #create wrapper object
    usdr = wvltSDR()

    #set parameters for RX
    usdr.setSampleRate(1E+6)
    usdr.setChannelMask(1)
    usdr.enableRXOnly()
    usdr.setRXFreq(100E+6)
    usdr.setRXFileOut('./pywrapper.out')
    usdr.setMaxBlocksCount()

    #start usdr_dm_create utility for RX
    print "starting (RX)..."
    p = usdr.start()
    if p:
        print "started ok"
    else:
        print "error!"
        exit(1)

    #wait for 10s, then stop it
    i = 0
    while usdr.checkState() is None and i < 10:
        time.sleep(1)
        i += 1

    if usdr.stop():
        print "stopped ok"
    else:
        print "not stopped, trying to kill..."
        if usdr.stop(force=True):
            print "killed"
        else:
            print "cannot kill"
            exit(11)

    #reset opts and set for TX
    usdr.resetOpts()
    usdr.setSampleRate(1E+6)
    usdr.setChannelMask(1)
    usdr.enableTXOnly()
    usdr.setRXFreq(100E+6)
    usdr.setTXFreq(900E+6)
    usdr.setBlocksCount(10)

    #start usdr_dm_create utility for TX
    print "starting (TX)..."
    p = usdr.start()
    if p:
        print "started ok"
    else:
        print "error!"
        exit(2)

    #wait for 10s and stop
    #BUT! since we set the number of blocks to 10, it should complete normally _before_ 10s has elapsed.
    i = 0
    while usdr.checkState() is None and i < 10:
        time.sleep(1)
        i += 1

    if usdr.stop():
        print "stopped ok"
    else:
        print "not stopped, trying to kill..."
        if usdr.stop(force=True):
            print "killed"
        else:
            print "cannot kill"
            exit(22)

    print "Bye bye"


if __name__ == '__main__': main()

