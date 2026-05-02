Simulator and JSON-RPC server Driver 

The driver is case insensitive for tag and signal names.

Names cannot start with @. Every thing starting with a digit is converted into a constant number.
Do not use the tags named put, get, subscribe, unsubscribe as they will interfere with the pseudo JSON parser.

Can be use for internal Tags with or without arithmetic/logic operations/signals.

A pusher can be link to a simulator tag, for instance boxdetection.
A infrared sensor can be link to the same simulator tag with the same name.
… et voila when a box is close to the sensor the pusher enter in action.

Arithmetic (+ - * / %) and logic (& | ^ !)  operations can be done with several members.
^ is xor not power.
In complex operations, operator precedence is not fully guaranteed, use ( ).
Members are either variables or constants.
For instance the pusher can be link to the tag name detector=sensor1 | !sensor2

Several equations can be written in a single line, separated by ; The used tag is the first one.
    var = a + b_c ; b_c = b*c      // This is an allowed comment in the tag declaration	
    var = a & b_c ; b_c = b | !c   // ! is the only additional tolerated operator for the members, but not before (.
    but could be done with var = a & (b | !c)

No loop can be directly done such as a=b+d then b=c-e and c=a*f. A tag in such loop is immediatly desactivated.
If a loop is required, uses the function prev(var) somewhere. Here for instance c=prev(a)*f. 
But a strange behaviour can occur depending how OIP calls sequencing is done to get and set tags.

6 analog simulation signals (all with a default output from 0 to 1) are available and can be used in tag.

    signal=sin(periode in seconds,temporal shift in seconds, amplitude, drift) such as 
	    var=sin(8.5) for a sin with a 8.5 seconds period with an output from 0 to 1
	    var=sin(8.5,2) for the same but with a phase shifted by 2 seconds -> f(t-2)
	    var=sin(8.5,2,4) for an output from 0 to 4 given by the multiplicator factor ->f(t-2)*4
	    var=sin(8.5,2,4,-1) for an output from -1 to 3 -> f(t-2)*4-1
	    all values are real, don't leave any empty value with two successive comma
	    ... a conveyor speed can be linked to such a signal to simulate motor instability.

    var=triangle(periode in seconds,temporal shift in seconds, amplitude, drift)
	    output from 0 to 1 between 0 to periode/2 then back to 0 between periode/2 to periode
    var=square(periode in seconds,temporal shift in seconds, amplitude, drift)
	    output 1 between 0 to periode/2 and output 0 during the remain time
    var=sawtooth(periode in seconds,temporal shift in seconds, amplitude, drift)
	    output from 0 to 1 then back to 0
    var=random(periode in seconds,temporal shift in seconds, amplitude, drift)
	    periode is the delay between two differents random number
    var=user(periode in seconds,temporal shift in seconds, amplitude, drift, val1, val2, ..... valx)
	    output is val1 then val2 up to valx during the periode at fixed change rate of periode/x

    With simulation signals, parameters can be a tag or a function :
	    var=sin(8,0,amplitude);amplitude=square(4) or var=sin(8,0,square(4))
	       ... with a partial sinus output when the square wave is not at 0.
	    var=user(4,0,1,0,var1,var2, 12, var4); var1=sin(0.1); var2=random(2); var4=4*var1
	       ...sinus, then random, then a const 12 an finally the same sinus with a multiplier
	    can be user(4,0,1,0,sin(0.1),random(2), 12, var4); var4=4*sin(0.1)
	    but NEVER use any logic or arithmetic operator with parameters
	    so previous call can be user(4,0,1,0,sin(0.1),random(2), 12, sin(0.1,0,4))
	    but CANNOT be user(4,0,1,0,sin(0.1),random(2), 12, 4*sin(0.1))

5 digital signals are available
           __________            _________            _________
     _____|   input  |__________|         |__________|         |_____
               ______            ______________        ______
     _________| Ton  |__________|     Toff     |______|Tpulse|____________
     
	    output=Ton(input,delay) 		// amplitude & drift can also be applied
	    output=Toff(input,delay)
	    output=Tpulse(input,delay)
	    
    Count add 1 each time the input value is different from the previous one.
    Count is a number of edges counter with digital tags.
    Input_count can be an integer (reals are converted to integer).
	    output=Count(input_count,input_reset)	
    	
    and RS/SR flip/flop latchs
	    output=RS(input_set,input_reset) 	// amplitude & drift can also be applied
	    output=SR(input_set,input_reset)

5 comparaison functions can be used :
	    output=EQ(var1,var2)	// return 1 if var1 is equal to var2, otherwise 0
	    output=GT(var1,var2)	// return 1 if var1 is superior to var2
	    output=GE(var1,var2)	// ... and so on ...
	    output=LT(var1,var2)
	    output=LE(var1,var2)	// const can be used
	    output=NE(var1,var2) 	// amplitude & drift can also be applied
	    
Finaly : prev(var) function getting the previous value rather than launching a calc (to break an infinite loop).

Counting for instance 3 rising edges can be done with
	    output=GE(inCount,5);inCount=count(input,reset) // 5 = 3 rising edges + 2 falling edges
	    or output=GE(count(input,reset),5)
Retentive timerOn can be build with
	    output=ton(inRTO,delay); inRTO=rs(input_set,input_reset)
	    or output=ton(rs(input_set,input_reset),delay)
Building an auto-reset counter
	    V=count(input,_reset); _reset=GE(prev(V),7);
	    or V=count(input,GE(prev(V),7))
   

A very basic text base protocol on udp port 55555 with the last (unique expected) client is integrated.
JSON RPC only with notifications (no request/response)
      -receive by the server
          subscribe message
              {"jsonrpc": "2.0", "method": "subscribe"}
              a data message notification will be send with all the tags/values just after
          write message
              {"jsonrpc": "2.0", "method": "put", "params":{"v1":value1,"v2":value2 ... }} in a single udp datagram.
          no response but if subscribe is done before (normaly it is) a value changed message is sent
          unsubscribe message, recommanded but not mandatory closing the socket is OK
              {"jsonrpc": "2.0", "method": "unsubscribe"}
      -send to the client (send values of uncalculated and not internal (const or other) Tags only)
              {"jsonrpc": "2.0", "method":"notify", "params":{"v1":value1,"v2":value2,....}} in multiples udp datagram
      -for debug purpose read one Tag even calculated can be done also with notification (no request id)
              {"jsonrpc": "2.0", "method": "get", "params":["varName"]} for the query notification
              a "method":"notify" message will be sent with this specific variable or an error
              {"jsonrpc": "2.0", "method": "tagnotfound","params":["varName"]}
Values are double numbers
 
The code here is not a json-rpc protocol checker. A lot of false frames can be sent, we don't care about it. 
For instance for put it just check the pattern "put" then it just find the second '{' then got the values.
So sending just "subscribe" or "unsubscribe" is OK and sending {"put" {"v1":value1,"v2":value2  is also OK 
but don't miss the two '{' and the quotes enclosing the method name.
So malformed messages are welcome, some working, some not. Real JSON-RPC messages are strongly recommended !


... And a simple python client code :

import json
import socket
import sys

OIP=("127.0.0.1", 55555)

UDPClientSocket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)

UDPClientSocket.sendto(str.encode('{"jsonrpc": "2.0", "method": "subscribe"}'), OIP)

# Write sample, nothing done if helloOIP does not exist
SendValue={"jsonrpc": "2.0", "method": "put", "params":{}}
SendValue["params"]["helloOIP"]=1
UDPClientSocket.sendto(str.encode(json.dumps(SendValue)), OIP)

UDPClientSocket.settimeout(1.0)

while True:
	try:
		data, addr = UDPClientSocket.recvfrom(1500)
		notify=json.loads(data.decode())
		params=notify["params"]
		print("")
		for value in params:
    			print(value," ",params[value])
	except:
		sys.stdout.write ('.')
		sys.stdout.flush()
