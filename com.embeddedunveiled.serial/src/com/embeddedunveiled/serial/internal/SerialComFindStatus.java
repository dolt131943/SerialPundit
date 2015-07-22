/**
 * Author : Rishi Gupta
 * 
 * This file is part of 'serial communication manager' library.
 *
 * The 'serial communication manager' is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * The 'serial communication manager' is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with serial communication manager. If not, see <http://www.gnu.org/licenses/>.
 */
package com.embeddedunveiled.serial.internal;

/**
 * <p>This class acts as a carrier of operation result between native and java layer.
 * If an error occurs native layer put error number into this status field
 * of this class.</p>
 */
public final class SerialComFindStatus {

	int status = 1; // package-private access

	/**
     * Construct and allocates a new SerialComFindStatus object with the integer value supplied.
     *
     * @param status initial status value
     */
	public SerialComFindStatus(int status) {
		this.status = status;
	}
	
}