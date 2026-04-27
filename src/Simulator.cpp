// Internal Tags database for OIP & basic JSON-RPC server
// interface similar to libplctag for a simple integration
// F Chaxel 2026

// Socket Windows & Linux adaptation
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
inline void socket_close(socket_t s) { closesocket(s); }
inline void socket_shutdown(socket_t s) { shutdown(s, SD_BOTH); }
using socklen_t = int;
inline int udp_sendto(socket_t s, const void* data, int len, const sockaddr* to, socklen_t tolen) {
    return ::sendto(s, (const char*)data, len, 0, to, tolen);
}
inline int udp_recvfrom(socket_t s, void* buf, int len, sockaddr* from, socklen_t* fromlen) {
    return ::recvfrom(s, (char*)buf, len, 0, from, fromlen);
}
static constexpr socket_t invalid_socket_v = INVALID_SOCKET;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <atomic>
using socket_t = int;
inline void socket_close(socket_t s) { ::close(s); }
inline void socket_shutdown(socket_t s) { shutdown(s, SHUT_RDWR); }
inline int udp_sendto(socket_t s, const void* data, int len, const sockaddr* to, socklen_t tolen) {
    return (int)::sendto(s, data, (size_t)len, 0, to, tolen);
}
inline int udp_recvfrom(socket_t s, void* buf, int len, sockaddr* from, socklen_t* fromlen) {
    return (int)::recvfrom(s, buf, (size_t)len, 0, from, fromlen);
}
static constexpr socket_t invalid_socket_v = -1;
#endif

#include <vector> 
#include <set>
#include <string>
#include <cstring>
#include <stdint.h>
#include <sstream>
#include <algorithm> 
#include <iostream>
#include <chrono>
#include <ctime>
#include <cmath> 
#include <thread>
#include <mutex>
#include <functional>
#include <stack>

#include "Simulator.hpp"

class SimTag
{
#define LOOPLIMIT 20

    friend class SimTagJsonRpcServer;

public :
    static std::vector<SimTag*> theTagsStorage;

private:

    static std::string functionNames[19];  
    static int InternalVarCounter;
    static long TimeStart;
    static bool Firstcall;

    // Tag value : Internal format for all data types from bit to integer 32 and real
    // data lost with integers 64 but not usefull for OIP
    // Val is also used to store some internal states when required by functions like random, timers, count ...
    std::atomic<double> Val = 0;
    bool IsConst = false;
    std::string Name;

    char _operator; // ten operators : + - * / % ^ | & ! # and ~ same as ! changed somwhere in the code. 
    // # and space (no op.) is for only one member such as var1=var2 ... could be done just with var2

    // var1=(var2+var3)*(var4-var5) ->  2 by 2 decomposition can be done by the user,
    // no problem for programmers.
    // var1=var1_1*var1_2; var1_1=var2+var3; var1_2=var4-var5
    SimTag *operandleft, *operandright;
    bool invertleft, invertright; // only used for boolean

    // When the Tag is a function signal
    double (SimTag::*func)(int Depth);
    // parameters for function as Tag (const 0 & 1 by default)
    // reused and "renamed" with #define when required for Timer & Bistable functions
    int funcPeriodeTag = 1, funcShiftTag=0, funcMagnitudeTag=1, funcFloorTag=0;
    std::vector<int> UserfuncValueTags; // for user sequence values ??applied to the output
    static bool Calc_Depth_reached;

    static std::function<void(int)> onWritelistener; // could be (a day) a vector
    
    static void static_init()
    {
        if (Firstcall)
        {
            onWritelistener = nullptr;
            Firstcall = false;
            // for funcPeriode, funcShift, ... default values
            SimTag::GetOrCreateTags("0"); // Tag identifier 0, value 0
            SimTag::GetOrCreateTags("1"); // Tag identifier 1, value 1
            // others const are automaticaly created via anonymous variables
            // when used such as var4=var3*2.5 --> 2.5 with value 2.5 is created.
            // Booleans cannot be convert by strtod(...). Don't know how they can be used, but ready.
            SimTag::GetOrCreateTags("true");theTagsStorage[2]->Val=1;
            SimTag::GetOrCreateTags("false");
            theTagsStorage[2]->IsConst = theTagsStorage[3]->IsConst = true;
        }
    }
    // private ctor only call from static SimTag::GetOrCreateTags(...)
    SimTag(std::string _Name, char __operator=' ', SimTag* leftop = nullptr, SimTag* rigthtop = nullptr, bool invleft = false, bool invright = false)
    {
        static_init();

        if (_Name.front() == '@') // anonymous Tag, cannot be reuse, normaly at least leftop is not null
            Name = "@"+std::to_string(InternalVarCounter++);
        else
            Name = _Name;

        // is it a function ?
        func = nullptr;
        for (int i=0;i<(int)std::size(functionNames);i++)
          if ((Name.find(functionNames[i]+"(")==0)&&(Name.back()==')'))
          {
            switch (i) // much readable to do than with an array of function pointers
            {
              case 0 : func = &SimTag::Random; double rd;memset(&rd,255,8);Val=rd;break;
              case 1 : func = &SimTag::Square;break;
              case 2 : func = &SimTag::Sin;break;
              case 3 : func = &SimTag::Triangle;break;
              case 4 : func = &SimTag::Sawtooth;break;
              case 5 : func = &SimTag::UserSignal;break;
              case 6 : func = &SimTag::TimerOn;break;
              case 7 : func = &SimTag::TimerOff;Val=-10000000;break;
              case 8 : func = &SimTag::TimerPulse;break;
              case 9 : func = &SimTag::Bistable_RS;break;
              case 10: func = &SimTag::Bistable_SR;break;
              case 11: func = &SimTag::Count; double ct;memset(&ct,255,8);Val=ct;break; 
              case 12: func = &SimTag::CompareEQ;break; // ==
              case 13: func = &SimTag::CompareGT;break; // >
              case 14: func = &SimTag::CompareGE;break; // >=
              case 15: func = &SimTag::CompareLT;break; // <
              case 16: func = &SimTag::CompareLE;break; // <=
              case 17: func = &SimTag::CompareNE;break; // !=
              case 18: func = &SimTag::Prev;break; 
            }
            extractFuncParamFromString(Name);
            if (i!=5)
              UserfuncValueTags.clear();

            return;
          }

        // constant values are welcome
        // not working with true & false, so pushed back before
        const char* str = Name.c_str();
        char* end;
        Val = std::strtod(str, &end);
        if (end != str) 
            IsConst = true;

        _operator = __operator;
        operandleft = operandright = nullptr;
        UpdateTag(_operator, leftop, rigthtop, invleft, invright);
    }
    void UpdateTag(char __operator, SimTag* leftop, SimTag* rigthtop, bool invleft, bool invright)
    {
        if ((_operator == '#') && (invleft == true)) // var1=~var2
            _operator = '!';
        else
            _operator = __operator;

        operandleft = leftop;
        operandright = rigthtop;
        invertleft = invleft;
        invertright = invright;

        if (operandleft == nullptr)
            _operator = (' '); // normaly it is.
        if ((_operator != ' ') && (_operator != '!') && (_operator != '#') && (operandright == nullptr))
            _operator = (' '); // normaly it is.
    }

public:
    static  double Simulator_readTag(int identifier)
    {
        if ((identifier >= 0) && (identifier < (int)SimTag::theTagsStorage.size()))
            return SimTag::theTagsStorage[identifier]->GetVal();

        return 0;

    }
    static int  Simulator_writeTag(int identifier, double Value)
    {
        if ((identifier >= 0) && (identifier < (int)SimTag::theTagsStorage.size()))
        {
            double valback=SimTag::theTagsStorage[identifier]->SetVal(Value); 
            
            // Notify the observer if the value is accepted and not internal
            if ((onWritelistener != nullptr) && (valback == Value) && (SimTag::theTagsStorage[identifier]->Name.front() != '@'))
                onWritelistener(identifier);

            return 0;
        }
        else
            return -1;
    }

    double SetVal(double v)
    {
        if (IsCalc())
            return v++; // used to indicate write was not done
        Val = v;
        return v;
    }

    double GetVal()
    {
        Calc_Depth_reached = false;

        double Val = GetVal(0);

        if (Calc_Depth_reached==true)
        {
            _operator = ' '; // Tag invalidation, no more calculated 
            func = nullptr;  // also if function
            return 0;
        }
        else
            return Val;
    }

    static int GetOrCreateTags(std::string MathOperations)
    {
        static_init();

        // remove comments in c++ or godot/python style
        size_t commentPos = MathOperations.find("//");
        if (commentPos != std::string::npos)
            MathOperations = MathOperations.substr(0, commentPos);
        commentPos = MathOperations.find("#");
        if (commentPos != std::string::npos)
            MathOperations = MathOperations.substr(0, commentPos);

        if (MathOperations.find('@') != std::string::npos) // reserved char
            return -1;

        // remove all space chars
        MathOperations.erase(std::remove(MathOperations.begin(), MathOperations.end(), ' '), MathOperations.end());

        // split if several lines of code are given (separated by ;)
        std::vector<std::string> elements = splitString(MathOperations);

        if (elements.size() == 0)
            return -1;

        int TagId = GetOrCreateTag(elements[0]);

        for (int i = 1; i < (int)elements.size(); i++)
            GetOrCreateTag(elements[i]);

        return TagId;
    }

    static void SimulationStart()
    {
        srand((int)time(NULL));
        TimeStart = 0;
        TimeStart = GetMsSimulation();
    }
private:
  
    bool IsCalc()
    {
        // consider a const is calculated
        return ((_operator != ' ') || (func!=nullptr));
    }
 
    double GetVal(int Depth)
    {
        if (IsConst)
          return Val;

        // here operandleft and operandright are required to be set correctly, test done in ctor call

        if (Depth > LOOPLIMIT) // infinite loop such as var1=var2+var3; var2=var1+var3;
        {
            Calc_Depth_reached = true;
            return 0;
        }

        if (func != nullptr)
            return (this->*func)(Depth+1) * theTagsStorage[funcMagnitudeTag]->GetVal(Depth + 1) 
                            + theTagsStorage[funcFloorTag]->GetVal(Depth + 1);

        if (IsCalc())
        { 
            if (_operator == '#') // No operator one element but not boolean such as var1=var2
                Val = operandleft->GetVal(Depth + 1);
            else if (_operator == '+')
                Val = operandleft->GetVal(Depth + 1) + operandright->GetVal(Depth + 1);
            else if (_operator == '-')
                Val = operandleft->GetVal(Depth + 1) - operandright->GetVal(Depth + 1);
            else if (_operator == '*')
                Val = operandleft->GetVal(Depth + 1) * operandright->GetVal(Depth + 1);
            else if (_operator == '/')
                // NaN, +inf, -inf are OK also to be converted to int or others (with strange results but no error)
                Val = operandleft->GetVal(Depth + 1) / operandright->GetVal(Depth + 1);
            else if (_operator == '%')
                Val = fmod(operandleft->GetVal(Depth + 1),operandright->GetVal(Depth + 1));
            else // boolean operators
            {
                bool V1 = (bool)operandleft->GetVal(Depth + 1);
                if (invertleft)
                    V1 = !V1;

                if (_operator == '!') // Only inversion
                {
                    Val = V1;
                    return Val;
                }

                bool V2 = (bool)operandright->GetVal(Depth + 1);
                if (invertright)
                    V2 = !V2;

                if (_operator == '&')
                    Val = (double)(V1 & V2);
                else if (_operator == '|')
                    Val = (double)(V1 | V2);
                else if (_operator == '^')
                    Val = (double)(V1 ^ V2);
                else
                    Val = false;    // should never occur
            }

        }
        return Val;
    }
    static int findTag(std::string Name)
    {
        for (int i = 0; i < (int)theTagsStorage.size(); i++)
        {
          if (theTagsStorage[i]->Name == Name)
                return i;
       }
       return -1;
    }
    static int GetOrCreateTag(std::string MathOperation)
    {
        // Don't accept this, it's very hard from the user to show it will create an hidden var
        if (MathOperation.front() == '=') return -1; // Another way could be MathOperation=MathOperation.substr(1)
        if ((MathOperation.front() == '!') || (MathOperation.front() == '~')) return -1;

        // all in lowercase
        std::transform(MathOperation.begin(), MathOperation.end(), MathOperation.begin(),
            [](unsigned char c) { return std::tolower(c); });
        // remove all space
        MathOperation.erase(std::remove(MathOperation.begin(), MathOperation.end(), ' '), MathOperation.end());

        // Get [] with var1 or var1,operator,var2 or var1,operator,var2,var3
        std::vector<std::string> components = parseMathOperation(MathOperation);

        // Works also with false sentence like "var1= var2+ " or "var1 = + var2" 
        // a variable "" is created with a zero value

        if (components.size() ==1 ) // Tag declaration
        {
            int idx = findTag(components[0]);
            if (idx != -1)
            {
                return idx;
            }
            else
            {
                //// No more exist, removed
                //if (components[0][0] == '!') // ~var1, creates a internal tag with operation
                //    return GetOrCreateTag("@="+components[0], Tags);

                SimTag* Tag = new SimTag(components[0]);
                theTagsStorage.push_back(Tag);
                return (int)theTagsStorage.size()-1;
            }

        }
        else if (components.size() == 2) // Tag declaration such as var1=var2 or var1=!var2 or var1=function(...)
        {
            int Tag1Id;

            if (components[1].front() == '!') // cannot be ~ : already modified 
                Tag1Id = GetOrCreateTag(components[1].substr(1));
            else
                Tag1Id = GetOrCreateTag(components[1]);

            int idx = findTag(components[0]);
            if (idx == -1) // New unknow Tag
            {
                SimTag* Tag;
                if (components[1].front() == '!')
                    Tag = new SimTag(components[0], '!', theTagsStorage[Tag1Id], nullptr, true, false);
                else
                    Tag = new SimTag(components[0], '#', theTagsStorage[Tag1Id], nullptr, false, false);
                theTagsStorage.push_back(Tag);
                return (int)theTagsStorage.size()-1;
            }
            else
            {
                // update existing tag : when a Tag is first created and defined after
                // var1=var2+var3 and var2=....
                // we do not known in which order OIP is creating tags
                // occur also between runs in OIP, with or without change of definition
                if (components[1].front() == '!')
                    theTagsStorage[idx]->UpdateTag('!', theTagsStorage[Tag1Id], nullptr, true, false);
                else
                    theTagsStorage[idx]->UpdateTag('#', theTagsStorage[Tag1Id], nullptr, false, false);

                return idx;
            }

        }
        else if (components.size() == 4) // Tag declaration & math operation
        {
            // Invertion for boolean value
            bool inv1 = false;
            if (components[2].front() == '!')
            {
                inv1 = true;
                components[2] = components[2].substr(1);
            }
            bool inv2 = false;
            if (components[3].front() == '!')
            {
                inv2 = true;
                components[3] = components[3].substr(1);
            }

            if ((components[0] == components[2]) || (components[0] == components[3]))
                return -1;

            int Tag1Id = GetOrCreateTag(components[2]);

            int Tag2Id;

            if (components[2] != components[3]) // can be var1 = var2 + var2, allowed here
                Tag2Id = GetOrCreateTag(components[3]);
            else
                Tag2Id = Tag1Id;

            int idx = findTag(components[0]);

            if (idx == -1) // New unknow Tag, add it
            {
                SimTag* Tag= new SimTag(components[0], components[1][0], theTagsStorage[Tag1Id], theTagsStorage[Tag2Id], inv1, inv2);
                theTagsStorage.push_back(Tag);
                return (int)theTagsStorage.size()-1;
            }
            else
            {
                // The Tag is known, update it
                theTagsStorage[idx]->UpdateTag(components[1][0], theTagsStorage[Tag1Id], theTagsStorage[Tag2Id], inv1, inv2);
                return idx;
            }
        }
        else
            return -1;
    }
    
    static long GetMsSimulation()
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return (long)(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() - TimeStart);
    }

    double Random(int Depth)
    {
        // The 64 bits double attribut Val 
        // avoid using an additionnal attribut in all objects for that
        struct RandMem 
        { 
            int32_t Val;        // rand() value
            int32_t PeriodeNum; // can be much smaller (some bits are ok)
        };
    
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        if (funcPeriode<=0)
            return (rand()/(double)RAND_MAX);
        
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);
        
        int timeMSeconds = (int)(GetMsSimulation() - funcShift * 1000);
        int periodeMs = (int)(funcPeriode * 1000);
        int PeriodeNum=timeMSeconds/periodeMs;  
            
        double ValShadow=Val; // Val is atomic
        RandMem* randVal=(RandMem *)&ValShadow;
        
        if (PeriodeNum==randVal->PeriodeNum) // same time period of the previous random number
            return (randVal->Val/(double)RAND_MAX);
        else
        {
            // generate a new random number
            randVal->Val=rand();
            randVal->PeriodeNum=PeriodeNum;
            Val=ValShadow;
            return (randVal->Val/(double)RAND_MAX);
        }
    }

    double Sin(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);
        if (funcPeriode <= 0) return 0;
        int timeMSeconds = int(GetMsSimulation() - funcShift * 1000);
        int periodeMs = (int)(funcPeriode * 1000);
        int position = timeMSeconds % periodeMs;

        double angle = (2.0 * 3.14159265358979323846 * position) / periodeMs;
        return (sin(angle)+1)/2;
    }
    double Square(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);
        if (funcPeriode <= 0) return 0;
        int timeMSeconds = (int)(GetMsSimulation() - funcShift * 1000);
        int periodeMs = (int)(funcPeriode * 1000);

        if ((timeMSeconds % periodeMs) < (periodeMs / 2))
            return 1;
        else
            return 0;
    }
    double Triangle(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        if (funcPeriode <= 0) return 0;
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);

        int timeMSeconds = (int)(GetMsSimulation() - funcShift * 1000);
        int periodeMs = (int)(funcPeriode * 1000);
        int position = timeMSeconds % periodeMs;

        int Half_periodeMs = (int)(periodeMs / 2.0);
        double position_relative = position % Half_periodeMs;

        if (position < Half_periodeMs)
            // from 0 to 1
            return position_relative / Half_periodeMs;
        else
            // from 1 to 0
            return 1.0 - position_relative / Half_periodeMs;
    }
    double Sawtooth(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        if (funcPeriode <= 0) return 0;
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);

        int timeMSeconds = (int)(GetMsSimulation() - funcShift * 1000);
        int periodeMs = (int)(funcPeriode * 1000);
        int position = timeMSeconds % periodeMs;
        return (double)position / (double)periodeMs;
    }
    double UserSignal(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth+1);
        if ((funcPeriode <= 0) || (UserfuncValueTags.size() == 0)) return 0;
        double funcShift = theTagsStorage[funcShiftTag]->GetVal();

        int timeMSeconds = (int)(GetMsSimulation() - funcShift * 1000);
        int periodeMs = (int)(funcPeriode * 1000);
        int position = timeMSeconds % periodeMs;
        int Idx = (int)((double)position / (double)periodeMs * UserfuncValueTags.size());
        return theTagsStorage[UserfuncValueTags.at(Idx)]->GetVal(Depth + 1);
    }

    #define funcInputTimerTag funcPeriodeTag
    #define funcInputSetTag funcPeriodeTag
    #define funcLeftTag funcPeriodeTag
    #define funcTimerDelayTag funcShiftTag
    #define funcInputResetTag funcShiftTag
    #define funcRightTag funcShiftTag   
    
    enum TimerType { Ton, Toff, Tpulse };
    //       __________            _________            _________
    // _____|   input  |__________|         |__________|         |_____
    //           ______            ___________dT_        __dT__
    // ______dT_| Ton  |__________|     Toff     |______|Tpulse|____________
  
    double Timers(int Depth, TimerType Typetimer)
    {
        double input = theTagsStorage[funcInputTimerTag]->GetVal(Depth + 1);
        // For Ton and Tpulse when off return off, for Toff when on return on
        if (((input==0)&&(Typetimer!=Toff))||((input!=0)&&(Typetimer==Toff)))
        {
            Val=(int)(GetMsSimulation()); // store the time in the attribut Val
            if (Typetimer!=Toff)
              return 0;
            else
              return 1;              
        }
        else // On for Ton & TPulse, Off for Toff
        {
            int timeMSeconds = (int)(GetMsSimulation()); 
           
            double funcDelay = theTagsStorage[funcTimerDelayTag]->GetVal(Depth + 1)*1000;
            
            // for Ton: return either off before the delay, on after the delay
            // for Tpulse: return either on before the delay, off after the delay,  
            // for Toff: return either on before the delay, off after the delay, 
            if (((timeMSeconds>=(funcDelay+Val))&&(Typetimer==Ton))||((timeMSeconds<(funcDelay+Val))&&(Typetimer!=Ton)))
                return 1;
            else
                return 0;
        }
    }
    
    double TimerOn(int Depth){ return Timers( Depth, Ton ); }  
    double TimerOff(int Depth) { return Timers( Depth, Toff ); }  
    double TimerPulse(int Depth) { return Timers( Depth, Tpulse ); }
    
    double Bistable(int Depth, bool IsRS)
    {
        bool input_set = theTagsStorage[funcInputSetTag]->GetVal(Depth + 1);
        bool input_reset = theTagsStorage[funcInputResetTag]->GetVal(Depth + 1);
        bool output=(bool)Val;
        
        if (IsRS)
            output= (output & (!input_reset) ) | input_set;
        else
            output= (output | input_set) & (!input_reset) ;
        
        Val=(double)output;
        
        return Val; 
    }
    double Bistable_RS(int Depth) { return Bistable(Depth, true); } double Bistable_SR(int Depth) { return Bistable(Depth, false); }
    
    double Count(int Depth) 
    { 
        // The 64 bits double attribut Val 
        // avoid using an additionnal attribut in all objects for that
        struct CountMem 
        { 
            uint32_t CountVal;
            int32_t PrevInput; 
        };
        
        double ValShadow=Val; // Val is atomic
        
        CountMem* countVal=(CountMem *)&ValShadow;
        
        double input_count = theTagsStorage[funcInputSetTag]->GetVal(Depth + 1);
        bool input_reset = theTagsStorage[funcInputResetTag]->GetVal(Depth + 1);
          
        if ((input_reset!=0) || (countVal->CountVal==0xFFFFFFFF))
        {
          countVal->CountVal=0;
          countVal->PrevInput=(int32_t)input_count; 
        }
        else if (countVal->PrevInput!=input_count)
        {
          countVal->CountVal++;             
          countVal->PrevInput= (int32_t)input_count;
        }
              
        Val=ValShadow;
         
        return countVal->CountVal;
    }
    double Compare(int Depth, int op)
    {
        double left=theTagsStorage[funcLeftTag]->GetVal(Depth + 1);
        double right=theTagsStorage[funcRightTag]->GetVal(Depth + 1);
        switch (op)
        {
          case 0: return left==right;
          case 1: return left>right;    
          case 2: return left>=right; 
          case 3: return left<right;          
          case 4: return left<=right;   
          case 5: return left!=right;   
        }
        return 0; // cannot be
    }
    double CompareEQ(int Depth) { return Compare(Depth,0);} double CompareGT(int Depth) { return Compare(Depth,1);} double CompareGE(int Depth) { return Compare(Depth,2);}
    double CompareLT(int Depth) { return Compare(Depth,3);} double CompareLE(int Depth) { return Compare(Depth,4);} double CompareNE(int Depth) { return Compare(Depth,5);}
        
    double Prev(int Depth)
    {
        return theTagsStorage[funcLeftTag]->Val;
    }
    
    std::vector<std::string> splitArgs(const std::string& s) {
        std::vector<std::string> result;
        std::string current;
        int depth = 0;

        size_t start = s.find('(');
        size_t end = s.rfind(')');
        if (start == std::string::npos || end == std::string::npos || end <= start)
            return result;

        for (size_t i = start + 1; i < end; ++i) {
            char c = s[i];

            if (c == '(') {
                depth++;
                current += c;
            }
            else if (c == ')') {
                depth--;
                current += c;
            }
            else if (c == ',' && depth == 0) {
                result.push_back(current);
                current.clear();
            }
            else {
                current += c;
            }
        }

        if (!current.empty())
            result.push_back(current);

        return result;
    }

    void extractFuncParamFromString(const std::string& params) 
    {
        std::vector<std::string> args= splitArgs(params);

        for (int i=0;i<args.size();i++)
        {
            switch (i)
            {
            case 0: funcPeriodeTag = GetOrCreateTag(args[i]); break;
            case 1: funcShiftTag = GetOrCreateTag(args[i]);  break;
            case 2: funcMagnitudeTag = GetOrCreateTag(args[i]);  break;
            case 3: funcFloorTag = GetOrCreateTag(args[i]); break;
            default: UserfuncValueTags.push_back(GetOrCreateTag(args[i])); // will be clear if it's not the User function
            }
        }
    }
    static std::vector<std::string> splitString(const std::string& input) 
    {
        std::vector<std::string> result;
        std::istringstream iss(input);

        std::string token;
        while (std::getline(iss, token, ';')) 
        {
            // don't put empty elements in the result
            if (!token.empty()) {
                result.push_back(token);
            }
        }

        return result;
    }
    static std::vector<std::string> split(const std::string& s, char delimiter) 
    {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) 
        {
            tokens.push_back(token);
        }
        return tokens;
    }
    static size_t findoperatoratlevel0(const std::string st)
    {
        int level = 0;

        for (size_t i = 0; i < st.size(); ++i)
        {
            if (st[i] == '(') level++;
            else if (st[i] == ')') level--;
            else if (level==0)
                if (st[i] == '+' || st[i] == '-' || st[i] == '*' || st[i] == '/' || st[i] == '%'
                    || st[i] == '^' || st[i] == '&' || st[i] == '|')
                    return i;
            if (level < 0) return std::string::npos;
        }
        return std::string::npos;
    }

    static std::vector<std::string> parseMathOperation(const std::string& operation)
    {
        std::vector<std::string> components;

        // Find the = symbol 
        size_t assignPos = operation.find('=');
        if (assignPos == std::string::npos)
            if (findoperatoratlevel0(operation) == std::string::npos) // it's a simple var usage "var1" or a function call
            {
                components.push_back(operation);
                return components;
            }
            else
                return parseMathOperation("@@=" + operation); // it's something like var1*var2 change it to add an anonymous variable

        // get affected variable (before = symbol)
        std::string leftVar = operation.substr(0, assignPos);

        if (!leftVar.empty())
            components.push_back(leftVar);
        else
            components.push_back("@@"); // anonymous variable

        // get the mathematical operation (after = symbol)
        std::string rightPart = operation.substr(assignPos + 1);

        // find the operator position
        size_t opPos = findoperatoratlevel0(rightPart);

        if (opPos == std::string::npos) 
        {
            // Return only the var name, but change ~ by !
            if (rightPart.front() == '~') rightPart.front() = '!';
            components.push_back(rightPart);
            return components; 
        }

        // copy the operator 
        std::string oper(1, rightPart[opPos]);
        components.push_back(oper);

        // cut in two : var names before and after the operator
        std::string rightVar1 = rightPart.substr(0, opPos);
        std::string rightVar2 = rightPart.substr(opPos + 1);

        components.push_back(rightVar1);
        components.push_back(rightVar2);

        return components;
    }

};

int SimTag::InternalVarCounter = 0;
long SimTag::TimeStart = 0;
bool SimTag::Firstcall = true;
bool SimTag::Calc_Depth_reached;
// functions names ... in lowercase only here !
std::string SimTag::functionNames[19]= {
    "random","square","sin","triangle","sawtooth","user",
    "ton","toff","tpulse","rs","sr", "count",
    "eq","gt","ge","lt","le","ne",
    "prev" };

std::vector<SimTag*> SimTag::theTagsStorage;
std::function<void(int)> SimTag::onWritelistener = nullptr;

// Network component
// Very basic text base protocol on udp port 55555 with the last (unique expected) client:
// JSON RPC only with notification (no request/response)
//     -receive by the server
//         subscribe message
//             {"jsonrpc": "2.0", "method": "subscribe"}
//             a data message notification will be send with all the tags/values just after
//         write message
//             {"jsonrpc": "2.0", "method": "put", "params":{"v1":value1,"v2":value2 ... }} in a single udp datagram (generaly only one tag value)
//           no response but if advise is done before (normaly it is) a value changed message is sent
//         unsubscribe message, not required closing the socket is OK
//             {"jsonrpc": "2.0", "method": "unsubscribe"}
//     -send to the client (send values of uncalculated and not internal (const or other) Tags only)
//             {"jsonrpc": "2.0", "method":"notify", "params":{"v1":value1,"v2":value2,....}} in multiples udp datagram
//     -for debug purpose read of Tag even calculated can be done also with notification (no request id)
//             {"jsonrpc": "2.0", "method": "get", "params":["varName"]} for the query notification
//             a "method":"notify" message will be sent with this specific variable/value or an error
//             {"jsonrpc": "2.0", "method": "tagnotfound","params":["varName"]}
// values are double numbers
//
// The code here is a not a json-rpc protocol checker. A lot of false frames can be sent, we don't care about it. 
// For instance for put it just check the word '"put"' then it just find the second '{' then got the values.
// So sending just advise or unadvise is OK and sending {"put" {"v1":value1,"v2":value2  is also OK (don't miss the two '{' and the quotes enclosing the method name)
// Malformed messages are welcome, some working, some not. Clean JSON - RPC messages are preferable !
class SimTagJsonRpcServer
{
    // UDP, 1 socket & thread
    std::thread UdpThread;
    socket_t udpSock;
    sockaddr_in clientudpAddr;
    std::atomic<bool> TerminateThreads;

    std::mutex SimTagUdpServerMutex; // TagIdToSend sharing
    bool sendAll = false;

    std::set<int> TagIdToSend;

public:
    SimTagJsonRpcServer()
    {
        TerminateThreads.store(false);
        clientudpAddr.sin_family = 0xFFFF;
        udpSock= invalid_socket_v;
        UdpThread = std::thread(&SimTagJsonRpcServer::UDP_loop, this);

        SimTag::onWritelistener = [this](int i) { this->OnNewTagValue(i); };
    }
    ~SimTagJsonRpcServer()
    {
        TerminateThreads.store(true);

        if (udpSock != invalid_socket_v)
        {
            socket_shutdown(udpSock);
            socket_close(udpSock);
        }

        UdpThread.join();
    }
    void OnNewTagValue(int identifier)
    {
        SimTagUdpServerMutex.lock();
        TagIdToSend.insert(identifier);
        SimTagUdpServerMutex.unlock();
    }

private:
    inline bool socket_readable_select(socket_t s, int timeout_ms)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);

        timeval tv{};
        timeval* ptv = nullptr;

        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            ptv = &tv;
        }

#ifdef _WIN32
        int r = ::select(0, &rfds, nullptr, nullptr, ptv);
        if (r == SOCKET_ERROR) return false;
#else
        int nfds = s + 1;
        int r = ::select(nfds, &rfds, nullptr, nullptr, ptv);
        if (r < 0) return false; // (option: g rer EINTR)
#endif

        return (r > 0) && (FD_ISSET(s, &rfds) != 0);
    }

    void UDP_loop()
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(55555);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        while (!TerminateThreads.load())
        {
            udpSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (udpSock == invalid_socket_v)
                return;
            
            if (::bind(udpSock, (sockaddr*)&addr, (int)sizeof(addr)) == 0)
            {
                for (;;)
                {
                    uint8_t bufrecp[1500];
                    sockaddr_in from{};
                    socklen_t fromLen = (socklen_t)sizeof(from);
                    int n;
                    
                    if (clientudpAddr.sin_family == 0xFFFF) // no subscribed client : receive in blocking mode
                      n = udp_recvfrom(udpSock, bufrecp, sizeof(bufrecp)-1, (sockaddr*)&from, &fromLen);
                    else if (socket_readable_select(udpSock, 10)) // wait receive with 10ms timeout
                      n = udp_recvfrom(udpSock, bufrecp, sizeof(bufrecp)-1, (sockaddr*)&from, &fromLen);

                    if (n < 0) break; // <0: error

                    if (n>0)
                    {
                      bufrecp[n] = 0;

                      std::string Msg((char*)bufrecp);

                      if (Msg.find("\"put\"") != std::string::npos)
                          WriteVars(std::string((char*)bufrecp));
                      if (Msg.find("\"get\"") != std::string::npos)
                          SendReadResponse(udpSock, from, std::string((char*)bufrecp));
                      else if (Msg.find("\"unsubscribe\"") != std::string::npos)
                      {
                          clientudpAddr.sin_family = 0xFFFF;
                          SimTagUdpServerMutex.lock();
                          TagIdToSend.clear();
                          SimTagUdpServerMutex.unlock();
                      }
                      else if (Msg.find("\"subscribe\"") != std::string::npos)
                      {
                          clientudpAddr = from;
                          SendVars(udpSock, true);
                      }
                    }
                    else
                    {
                        SendVars(udpSock, false); 
                    }
                }
            }
            else
                socket_close(udpSock);

            if (TerminateThreads.load())
                return;

            // unable to bind, certainly another server, try later
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    std::string JsonHeader(std::string methodName)
    {
        return "{\"jsonrpc\":\"2.0\",\"method\":\""+methodName+"\",\"params\":";
    }
    void SendVars(socket_t s, bool AllValues)
    {
        if (clientudpAddr.sin_family == 0xFFFF)
            return;

        SimTagUdpServerMutex.lock();

        if (AllValues)
        {
            TagIdToSend.clear();
            for (int i = 0; i < (int)SimTag::theTagsStorage.size(); i++)
                if ((SimTag::theTagsStorage[i]->IsConst == false)&&(SimTag::theTagsStorage[i]->IsCalc() == false)&&(SimTag::theTagsStorage[i]->Name[0]!='@'))
                    TagIdToSend.insert(i);
        }

        std::string RepJson = JsonHeader("notify")+"{";

        for (int identifier : TagIdToSend)
        {

            std::string newVal = "\"" + SimTag::theTagsStorage[identifier]->Name + "\":" + to_string_trim(SimTag::theTagsStorage[identifier]->Val);
            
            if (RepJson.size()+newVal.size()<=1450) // udp up to 1452 on ipv6 
                RepJson = RepJson + newVal + ',';
            else
            {
                RepJson.back() = '}'; RepJson += '}';
                if (udp_sendto(udpSock, RepJson.data(), (int)RepJson.size(), (sockaddr*)&clientudpAddr, (socklen_t)sizeof(sockaddr_in))<=0)
                {
                  clientudpAddr.sin_family = 0xFFFF;
                  SimTagUdpServerMutex.unlock();
                  return;
                }
                RepJson = JsonHeader("notify")+"{"+newVal+",";
            }
        }
        TagIdToSend.clear();
        SimTagUdpServerMutex.unlock();

        if ((RepJson.size() >0)  && (RepJson.back() == ','))
        {
            RepJson.back() = '}'; RepJson += '}';
            if (udp_sendto(udpSock, RepJson.data(), (int)RepJson.size(), (sockaddr*)&clientudpAddr, (socklen_t)sizeof(sockaddr_in)) <= 0)
                clientudpAddr.sin_family = 0xFFFF;
        }
    }

    std::string to_string_trim(double x)
    {
        std::string s = std::to_string(x); // ex: "12.400000" ou "1.000000"

        auto dot = s.find('.');
        if (dot == std::string::npos) return s;

        while (!s.empty() && s.back() == '0')
            s.pop_back();

        if (!s.empty() && s.back() == '.')
            s.pop_back();

        if (s == "-0") s = "0";

        return s;
    }
    static bool parse_quoted_string(const std::string& s, std::size_t& i, std::string& out)
    {
        if (i >= s.size() || s[i] != '"') return false;
          i++; // skip opening "

        std::size_t start = i;
        while (i < s.size() && s[i] != '"') {
            i++;
        }
        if (i >= s.size()) return false; // no closing "

        out = s.substr(start, i - start);
        i++; // skip closing "
        return true;
    }
    static bool parse_number_token(const std::string& s, std::size_t& i, std::string& out)
    {
        if (i >= s.size()) return false;

        std::size_t start = i;
        while (i < s.size()
            && s[i] != ','
            && s[i] != '}')
        {
            i++;
        }

        out = s.substr(start, i - start);
        return !out.empty();
    }
    static bool skip_char(const std::string& s, std::size_t& i, char c) 
    { 
        for (;;)
        {
            if (i < s.size() && s[i] == c)
            {
                i++;
                return true;
            }
            if (i==s.size())
                return false;
            i++;
        }
    }
    // Generaly only one var at a time
    void WriteVars(const std::string& json)
    {

        std::string line(json);

        std::size_t i = 0;

        // remove all spaces and specials chars
        line.erase(std::remove_if(line.begin(),line.end(),[](char c) 
        { return std::isspace(static_cast<unsigned char>(c)); }),
        line.end());

        if (!skip_char(line, i, '{')) // first {
            return;
        if (!skip_char(line, i, '{')) // second { for parameters
            return;

        while (true) {
            // comma
            if (i < line.size() && line[i] == ',') { ++i; continue; }
            if (i >= line.size()) break;

            // name
            std::string name;
            if (!parse_quoted_string(line, i, name)) {
                return;
            }

            if (!skip_char(line, i, ':')) {
                return;
            }

            std::string numTok;
            if (!parse_number_token(line, i, numTok)) 
            {
                return;
            }

            int Idx = SimTag::findTag(name);
            if (Idx!=-1)
                if ((SimTag::theTagsStorage[Idx]->_operator == ' ') && (SimTag::theTagsStorage[Idx]->IsCalc() == false))
                {
                    double value = std::atof(numTok.c_str());
                    SimTag::Simulator_writeTag(Idx, value); // undirect write, so pushed back in the TagIdToSend list
                }

            if (i >= line.size()) return;

            if (line[i] == ',') { i++; continue; }
            if (line[i] == '}') { return; }
        }
    }
    void SendReadResponse(socket_t s, sockaddr_in sender, const std::string& json)
    {
        std::string line(json);

        std::size_t i = 0;
     
        // remove all spaces and specials chars
        line.erase(std::remove_if(line.begin(),line.end(),[](char c) 
        { return std::isspace(static_cast<unsigned char>(c)); }),
        line.end());

        if (!skip_char(line, i, '[')) // search [ for parameter
            return;
 
        std::string nameTok;
        if (!parse_quoted_string(line, i, nameTok))
            return;

        int Idx = SimTag::findTag(nameTok);
        std::string RepJson;
        
        if (Idx!=-1)
          // return Val, not GetVal(), should be updated by OIP tag_get_xxx during simulation
          RepJson = JsonHeader("notify")+"{"+"\""+nameTok+"\":"+to_string_trim(SimTag::theTagsStorage[Idx]->Val)+"}}";
        else
          RepJson = JsonHeader("tagnotfound")+"["+"\""+nameTok+"\"]}";
          
        udp_sendto(udpSock, RepJson.data(), (int)RepJson.size(), (sockaddr*)&sender, (socklen_t)sizeof(sockaddr_in));
    }
};

int UserdbTagCount = 0; // Used to count the number of register/unregister calls

bool SimulatorSrv_Up = false;
SimTagJsonRpcServer* SrvSimulator;

///////////////////////////////////////////////////////////
// Begin of the interface functions similar to libplctag //
///////////////////////////////////////////////////////////
int Simulator_tag_create(const char* MathOperation)
{
    int retcode = SimTag::GetOrCreateTags(MathOperation);

    if (SimulatorSrv_Up == false)
    {
#if defined(_WIN32)
        WSADATA wsaData;
        std::ignore = WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        SimulatorSrv_Up = true;
        SrvSimulator = new SimTagJsonRpcServer();
    }

    if (UserdbTagCount++ <= 0)
        SimTag::SimulationStart();  

    return retcode;
}

int Simulator_tag_destroy(int identifier)
{
    UserdbTagCount--;
    // Not needed : small quantity, short duration, check dependencies required, identifier is index ... so no
    // only (external) tag usage count to reset the simulation time
    // could be done with an usage counter associate to every Tag and manual garbadge collector
    return 0;
}
int Simulator_tag_write(int identifier)
{
    // Write already done with set.
    return 0;
}
int Simulator_tag_read(int identifier)
{
            return 0;
}

int Simulator_tag_get_bit(int identifier)
{
    return (bool)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_bit(int identifier, int val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

uint64_t Simulator_tag_get_uint64(int identifier)
{
    return (uint64_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_uint64(int identifier, uint64_t val)
{
    return SimTag::Simulator_writeTag(identifier, (double)val);
}

int64_t Simulator_tag_get_int64(int identifier)
{
    return (int64_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_int64(int identifier, int64_t val)
{
    return SimTag::Simulator_writeTag(identifier, (double)val);
}

uint32_t Simulator_tag_get_uint32(int identifier)
{
    return (uint32_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_uint32(int identifier, uint32_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

int32_t Simulator_tag_get_int32(int identifier)
{
    return (int32_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_int32(int identifier, int32_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

uint16_t Simulator_tag_get_uint16(int identifier)
{
    return (uint16_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_uint16(int identifier, uint16_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

int16_t Simulator_tag_get_int16(int identifier)
{
    return (int16_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_int16(int identifier, int16_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

uint8_t Simulator_tag_get_uint8(int identifier)
{
    return (uint8_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_uint8(int identifier, uint8_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

int8_t Simulator_tag_get_int8(int identifier)
{
    return (int8_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_int8(int identifier, int8_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

double Simulator_tag_get_float64(int identifier)
{
    return (double)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_float64(int identifier, double val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}

float Simulator_tag_get_float32(int identifier)
{
    return (float)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_float32(int identifier, float val)
{
    return SimTag::Simulator_writeTag(identifier, val);
}
