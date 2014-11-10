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
 * This class encapsulate data received from serial port. Application can call getDataBytes() method
 * on an instance of this class to retrieve data.
 */

public final class SerialComDataEvent {
	
	private byte[] mData = null;

    public SerialComDataEvent(byte[] data){
    	this.mData = data;
    }
    
    public byte[] getDataBytes() {
    	return mData;
    }
}
