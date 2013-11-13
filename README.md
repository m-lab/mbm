# mbm #
[![Build Status](https://drone.io/github.com/m-lab/mbm/status.png)](https://drone.io/github.com/m-lab/mbm/latest)

## wire protocol ##
### diagram ###
![Wire Protocol](wire_protocol.png)

### structures ###
#### Config ####

<table>
  <tr><th>offset (bytes)</th><th>field           </th><th>accepted values </th><th>width </th></tr>
  <tr><td>0             </td><td>test protocol   </td><td>0 (TCP), 1 (UDP)</td><td>32-bit</td></tr>
  <tr><td>4             </td><td>bit rate kbits/s</td><td>0 - INT_MAX-1   </td><td>32-bit</td></tr>
  <tr><td>8             </td><td>loss threshold  </td><td>0.0 - 1.0       </td><td>64-bit</td></tr>
</table>

#### Port ####

<table>
  <tr><th>offset (bytes)</th><th>field    </th><th>accepted values</th><th>width </th></tr>
  <tr><td>0             </td><td>test port</td><td>0 - 65535      </td><td>16-bit</td></tr>
</table>

#### Ready ####

<table>
  <tr><th>offset (bytes)</th><th>field</th><th>accepted values</th><th>width </th></tr>
  <tr><td>0             </td><td>ready</td><td>"READY"        </td><td>30-bit</td></tr>
</table>

#### Chunk length ####

<table>
  <tr><th>offset (bytes)</th><th>field       </th><th>accepted values</th><th>width </th></tr>
  <tr><td>0             </td><td>chunk length</td><td>0 - INT_MAX-1  </td><td>32-bit</td></tr>
</table>

#### Data ####

<table>
  <tr><th>offset (bytes)</th><th>field          </th><th>accepted values</th><th>width </th></tr>
  <tr><td>0             </td><td>sequence number</td><td>0 - INT_MAX-1  </td><td>32-bit</td></tr>
  <tr><td>4             </td><td>padding        </td><td>*              </td><td>chunk length - 32-bit</td></tr>
</table>

#### Result ####

<table>
  <tr><th>offset (bytes)</th><th>field </th><th>accepted values                                </th><th>width </th></tr>
  <tr><td>0             </td><td>result</td><td>0 (FAIL), 1 (PASS), 2 (INCONCLUSIVE), 3 (ERROR)</td><td>32-bit</td></tr>
</table>
