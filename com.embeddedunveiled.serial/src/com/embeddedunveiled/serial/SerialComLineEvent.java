/*
 * Author : Rishi Gupta
 * Email  : gupt21@gmail.com
 * 
 * This file is part of 'serial communication manager' program.
 *
 * 'serial communication manager' is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * 'serial communication manager' is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with serial communication manager. If not, see <http://www.gnu.org/licenses/>.
 */

package com.embeddedunveiled.serial;

/**
 * This class encapsulate events that happens on serial port control lines. Application can call various
 * methods on an instance of this class to know which event has happen and to get its details.
 */

public final class SerialComLineEvent {

	private int mOldLineEvent = -1;
	private int mNewLineEvent = -1;
	private int mChanged = -1;
	
    public SerialComLineEvent(int oldLineState, int newLineState) {
    	mOldLineEvent = oldLineState;
    	mNewLineEvent = newLineState;
    	mChanged = mOldLineEvent ^ mNewLineEvent;					// XOR old with new state to find the one(s) that changed
    }
    
    public int getCTS() {
        if ((mChanged & SerialComManager.CTS) == 1) {				// CTS has changed
            if ((mNewLineEvent  & SerialComManager.CTS) == 1) {
                return 1; 											// CTS went from 0 to 1
            } else {
                return 2; 											// CTS went from 1 to 0
            }
        } else {
        	return 0;     											// CTS is not changed
        }
    }
    
    public int getDSR() {
        if ((mChanged & SerialComManager.DSR) == 1) {				// DSR has changed
            if ((mNewLineEvent  & SerialComManager.DSR) == 1) {
                return 1; 											// DSR went from 0 to 1
            } else {
                return 2; 											// DSR went from 1 to 0
            }
        } else {
        	return 0;     											// DSR is not changed
        }
    }
    
    public int getDCD() {
        if ((mChanged & SerialComManager.DCD) == 1) {				// DCD has changed
            if ((mNewLineEvent  & SerialComManager.DCD) == 1) {
                return 1; 											// DCD went from 0 to 1
            } else {
                return 2; 											// DCD went from 1 to 0
            }
        } else {
        	return 0;     											// DCD is not changed
        }
    }
    
    public int getRI() {
        if ((mChanged & SerialComManager.RI) == 1) {				// RI has changed
            if ((mNewLineEvent  & SerialComManager.RI) == 1) {
                return 1; 											// RI went from 0 to 1
            } else {
                return 2; 											// RI went from 1 to 0
            }
        } else {
        	return 0;     											// RI is not changed
        }
    }
    
}
