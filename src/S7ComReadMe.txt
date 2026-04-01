Drivers for S7 1200 and 1500, not for very old Siemens PLC such as 200/300/400.

PUT/GET protocol MUST be validated with Tia Portal before : it's not the default value.
See that on "Security features" close to the Cpu configuration parameters (Ip, …).

Data exchange occurs only in memory spaces I, Q, and M.
Within each space, data is read from the lowest requested address to the highest one.
Use the most contiguous blocks possible: MB0 and MB200 are not a good idea for optimizing communication.

For sensors in OIP simulation, I Input space can be used if it do not overlap with the actual inputs 
of the local racks or Profinet remotes IO. It is more natural for the PLC code to access the 
simulated sensor as I80.4 rather than M80.4! OIP is writing Input on the Plc.
Q output space can also be used to drive actuators in OIP simulation. Overlap or not with the rack/Profinet outputs.
