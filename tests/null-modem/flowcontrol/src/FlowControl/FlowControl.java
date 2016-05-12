/*
 * Author : Rishi Gupta
 * 
 * This file is part of 'serial communication manager' library.
 * Copyright (C) <2014-2016>  <Rishi Gupta>
 *
 * This 'serial communication manager' is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * The 'serial communication manager' is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR 
 * A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with 'serial communication manager'.  If not, see <http://www.gnu.org/licenses/>.
 */

package FlowControl;

import com.embeddedunveiled.serial.SerialComManager;
import com.embeddedunveiled.serial.nullmodem.SerialComNullModem;
import com.embeddedunveiled.serial.SerialComManager.BAUDRATE;
import com.embeddedunveiled.serial.SerialComManager.DATABITS;
import com.embeddedunveiled.serial.SerialComManager.FLOWCONTROL;
import com.embeddedunveiled.serial.SerialComManager.PARITY;
import com.embeddedunveiled.serial.SerialComManager.STOPBITS;

class Reader implements Runnable {
    SerialComManager scm = null;
    long handle = 0;
    public Reader(SerialComManager scm, long handle) {
        this.scm = scm;
        this.handle = handle;
    }
    @Override
    public void run() {
        try {
            for(int x=0; x<8; x++) {
                Thread.sleep(2000);
                System.out.println("Read : " + scm.readString(handle, 2*1024));
            }
        }catch (Exception e) {
            e.printStackTrace();
        }
    }
}

public final class FlowControl {

    public static void main(String[] args) throws Exception {

        SerialComManager scm = new SerialComManager();
        final SerialComNullModem scnm = scm.getSerialComNullModemInstance();
        Thread t = null;

        try {
            scnm.createStandardNullModemPair(-1, -1);
            String[] ports = scnm.getLastNullModemDevicePairNodes();
            Thread.sleep(200);

            // READ AND WRITE
            long handle0 = scm.openComPort(ports[0], true, true, true);
            scm.configureComPortData(handle0, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_ODD, BAUDRATE.B115200, 0);
            scm.configureComPortControl(handle0, FLOWCONTROL.NONE, 'x', 'x', true, true);

            long handle1 = scm.openComPort(ports[1], true, true, true);
            scm.configureComPortData(handle1, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_ODD, BAUDRATE.B115200, 0);
            scm.configureComPortControl(handle1, FLOWCONTROL.NONE, 'x', 'x', true, true);

            scm.writeString(handle0, "test", 0);
            Thread.sleep(100);

            String str = scm.readString(handle1);
            System.out.println(str);

            scm.closeComPort(handle0);
            scm.closeComPort(handle1);
            System.out.println("\nTEST 1 DONE !\n");

            // SOFTWARE FLOW CONTROL (driver will send xoff and then xon)
            ports = scnm.createStandardNullModemPair(-1, -1);
            Thread.sleep(200);

            long handle00 = scm.openComPort(ports[0], true, true, false);
            scm.configureComPortData(handle00, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_NONE, BAUDRATE.B115200, 0);
            scm.configureComPortControl(handle00, FLOWCONTROL.XON_XOFF, (char)50, (char)51, false, false);

            long handle11 = scm.openComPort(ports[1], true, true, false);
            scm.configureComPortData(handle11, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_NONE, BAUDRATE.B115200, 0);
            scm.configureComPortControl(handle11, FLOWCONTROL.XON_XOFF, (char)50, (char)51, false, false);

            byte[] buffer = new byte[2*1024];
            for(int x=0; x<2048; x++) {
                buffer[x] = (byte)65;
            }

            t = new Thread(new Reader(scm, handle11));
            t.start();

            try {
                System.out.println("1 : " + scm.writeBytes(handle00, buffer));
                System.out.println("2 : " + scm.writeBytes(handle00, buffer));
                Thread.sleep(100);
                System.out.println("6 : " + scm.writeBytes(handle00, buffer));
                System.out.println("3 : " + scm.writeBytes(handle00, buffer));
                Thread.sleep(100);
                System.out.println("4 : " + scm.writeBytes(handle00, buffer));
                Thread.sleep(5000);
                System.out.println("5 : " + scm.writeBytes(handle00, buffer));
                Thread.sleep(5000);
                System.out.println("6 : " + scm.writeBytes(handle00, buffer));
                System.out.println("6 : " + scm.writeBytes(handle00, buffer));
                Thread.sleep(10000);
            }catch(Exception e) {
                e.printStackTrace();
            }
            // all strings will be received
            try {
                scm.clearPortIOBuffers(handle00, true, true);
                scm.clearPortIOBuffers(handle11, true, true);

                scm.writeString(handle00, "teststr21", 0);
                scm.writeBytes(handle00, buffer);
                Thread.sleep(100);
                System.out.println("Data2a : " + scm.readString(handle11, 2*1024));
                scm.writeString(handle00, "teststr22", 0);
                scm.writeBytes(handle00, buffer);
                Thread.sleep(100);
                System.out.println("Data2b : " + scm.readString(handle11, 2*1024));
                scm.writeString(handle00, "teststr23", 0);
                Thread.sleep(100);
                System.out.println("Data2c : " + scm.readString(handle11, 2*1024));
            }catch(Exception e) {
                e.printStackTrace();
            }
            scm.closeComPort(handle00);
            scm.closeComPort(handle11);
            System.out.println("\nTEST 2 DONE !\n");

            // HARDWARE FLOW CONTROL
            ports = scnm.createStandardNullModemPair(-1, -1);
            Thread.sleep(200);

            long handle02 = scm.openComPort(ports[0], true, true, true);
            scm.configureComPortData(handle02, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_NONE, BAUDRATE.B115200, 0);
            scm.configureComPortControl(handle02, FLOWCONTROL.RTS_CTS, 'x', 'x', false, false);

            long handle12 = scm.openComPort(ports[1], true, true, true);
            scm.configureComPortData(handle12, DATABITS.DB_8, STOPBITS.SB_1, PARITY.P_NONE, BAUDRATE.B115200, 0);
            scm.configureComPortControl(handle12, FLOWCONTROL.RTS_CTS, 'x', 'x', false, false);

            byte[] buffer1 = new byte[2*1024];
            for(int x=0; x<2048; x++) {
                buffer1[x] = (byte)65;
            }

            t = new Thread(new Reader(scm, handle12));
            t.start();

            try {
                System.out.println("1 : " + scm.writeBytes(handle02, buffer1));
                System.out.println("2 : " + scm.writeBytes(handle02, buffer1));
                Thread.sleep(100);
                System.out.println("6 : " + scm.writeBytes(handle02, buffer1));
                System.out.println("3 : " + scm.writeBytes(handle02, buffer1));
                Thread.sleep(100);
                System.out.println("4 : " + scm.writeBytes(handle02, buffer1));
                Thread.sleep(100);
                System.out.println("5 : " + scm.writeBytes(handle02, buffer1));
                Thread.sleep(100);
                System.out.println("6 : " + scm.writeBytes(handle02, buffer1));
                System.out.println("6 : " + scm.writeBytes(handle02, buffer1));
                Thread.sleep(10000);
            }catch(Exception e) {
                e.printStackTrace();
            }
            scm.closeComPort(handle02);
            scm.closeComPort(handle12);

            System.out.println("\nTEST 3 DONE !\n");

            //scnm.destroyAllVirtualDevices();
            scnm.releaseResources(); 
            System.out.println("Done !");
        }catch (Exception e) {
            e.printStackTrace();
        }
    }
}