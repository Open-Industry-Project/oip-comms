// Internal Tags database for OIP
// interface similar to libplctag for a simple integration
// F Chaxel 2026

#include <vector> 
#include <string>
#include <cstring>
#include <stdint.h>
#include <sstream>
#include <algorithm> 
#include <iostream>
#include <chrono>
#include <ctime>
#include <cmath> 

#include "Simulator.hpp"

class SimTag
{
#define LOOPLIMIT 20

public :
    static std::vector<SimTag*> theTagsStorage;

private:
    static int InternalVarCounter;
    static long TimeStart;
    static bool Firstcall;

    // Tag value : Internal format for all data types from bit to integer 32 and real
    // data lost with integers 64 but not usefull for OIP
    double Val = 0;
    bool IsConst = false;
    std::string Name;

    char _operator; // ten operators : + - * / ^ | & ! # and ~ same as ! changed somwhere in the code. 
    // # (no op.) is for only one member such as var1=var2 ... could be done just with var2

    // var1=(var2+var3)*(var4-var5) ->  2 by 2 decomposition can be done by the user,
    // no problem for programmers. Certainly not required in OIP.
    // var1=var1_1*var1_2; var1_1=var2+var3; var1_2=var4-var5
    SimTag *operandleft, *operandright;
    bool invertleft, invertright; // only used for boolean

    // When the Tag is a periodic function signal
    double (SimTag::*func)(int Depth);
    // parameters for function as Tag (const 0 & 1 by default)
    int funcPeriodeTag = 1, funcShiftTag=0, funcMagnitudeTag=1, funcFloorTag=0;
    std::vector<int> UserfuncValueTags; // for user sequence values ??applied to the output
    static bool Calc_Depth_reached;

    static void static_init()
    {
        if (Firstcall)
        {
            Firstcall = false;
            // for funcPeriode, funcShift, ... default values
            SimTag::GetOrCreateTags("0"); // Tag identifier 0, value 0
            SimTag::GetOrCreateTags("1"); // Tag identifier 1, value 1
            // others const are automaticaly created via anonymous variables
            // when used such as var4=var3*2.5 --> 2.5 with value 2.5 is created.
            // Booleans cannot be convert. Don't know how they can be used ... but ready.
            SimTag::Simulator_writeTag(SimTag::GetOrCreateTags("true"), 1);
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

        if ((Name.substr(0, 7) == "random(")&&(Name.back()==')'))
        {
            extractFuncParamFromString(Name.substr(7));
            func = &SimTag::Random;
            UserfuncValueTags.clear();
        }
        else if ((Name.substr(0, 7) == "square(") && (Name.back() == ')'))
        {
            extractFuncParamFromString(Name.substr(7));
            func = &SimTag::Square;
            UserfuncValueTags.clear();
        }
        else if ((Name.substr(0, 4) == "sin(") && (Name.back() == ')'))
        {
            extractFuncParamFromString(Name.substr(4));
            func = &SimTag::Sin;
            UserfuncValueTags.clear();
        }
        else if ((Name.substr(0, 9) == "triangle(") && (Name.back() == ')'))
        {
            extractFuncParamFromString(Name.substr(9));
            func = &SimTag::Triangle;
            UserfuncValueTags.clear();
        }
        else if ((Name.substr(0, 9) == "sawtooth(") && (Name.back() == ')'))
        {
            extractFuncParamFromString(Name.substr(9));
            func = &SimTag::Sawtooth;
            UserfuncValueTags.clear();
        }
        else if ((Name.substr(0, 5) == "user(") && (Name.back() == ')'))
        {
            extractFuncParamFromString(Name.substr(5));
            func = &SimTag::UserSignal;
        }
        else
            func = nullptr;

        if (funcMagnitudeTag == 0) funcMagnitudeTag = 1; // certainly a user mistake


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
        if ((identifier >= 0) && (identifier < SimTag::theTagsStorage.size()))
            return SimTag::theTagsStorage[identifier]->GetVal();

        return (uint64_t)0;

    }
    static int  Simulator_writeTag(int identifier, double Value)
    {
        if ((identifier >= 0) && (identifier < SimTag::theTagsStorage.size()))
        {
            SimTag::theTagsStorage[identifier]->SetVal(Value); // it don't care if the Tag is compute or not
            return 0;
        }
        else
            return -1;
    }

    void SetVal(double v)
    {
        if ((IsConst == true)||(func!=nullptr))
            return;
        Val = v; 
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

        // remove comments in c++ or godot style
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

        for (int i = 1; i < elements.size(); i++)
            GetOrCreateTag(elements[i]);

        return TagId;
    }

    static void SimulationStart()
    {
        srand(time(NULL));
        TimeStart = 0;
        TimeStart = GetMsSimulation();
    }
private:
  
    bool IsCalc()
    {
        return _operator != ' ';
    }
 
    double GetVal(int Depth)
    {
        // here operandleft and operandright are required to be set correctly, test done in ctor call

        if (Depth > LOOPLIMIT) // infinite loop such as var1=var2+var3; var2=var1+var3;
        {
            Calc_Depth_reached = true;
            return 0;
        }

        if (func != nullptr)
            return (this->*func)(Depth+1) * theTagsStorage[funcMagnitudeTag]->GetVal(Depth + 1) + theTagsStorage[funcFloorTag]->GetVal(Depth + 1);

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
    static int findTag(std::vector<SimTag*>& TagsStorage,std::string Name)
    {
        for (int i = 0; i < TagsStorage.size(); i++)
            if (TagsStorage[i]->Name == Name)
                return i;
       return -1;
    }
    static int GetOrCreateTag(std::string MathOperation)
    {
        SimTag* Tag;

        // Don't accept this, it's very hard from the user to show it will create an hidden var
        if (MathOperation.front() == '=') return -1; // Another way could be MathOperation=MathOperation.substr(1)
        if ((MathOperation.front() == '!') || (MathOperation.front() == '~')) return -1;

        // all in lowercase
        std::transform(MathOperation.begin(), MathOperation.end(), MathOperation.begin(),
            [](unsigned char c) { return std::tolower(c); });

        // Get [] with var1 or var1,operator,var2 or var1,operator,var2,var3
        std::vector<std::string> components = parseMathOperation(MathOperation);

        // Works also with false sentence like "var1= var2+ " or "var1 = + var2" 
        // a variable "" is created with a zero value

        if (components.size() ==1 ) // Tag declaration
        {
            int idx = findTag(theTagsStorage,components[0]);
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
                return theTagsStorage.size()-1;
            }

        }
        else if (components.size() == 2) // Tag declaration such as var1=var2 or var1=!var2
        {
            int Tag1Id;

            if (components[1].front() == '!') // cannot be ~ : already modified 
                Tag1Id = GetOrCreateTag(components[1].substr(1));
            else
                Tag1Id = GetOrCreateTag(components[1]);

            int idx = findTag(theTagsStorage,components[0]);
            if (idx == -1) // New unknow Tag
            {
                SimTag* Tag;
                if (components[1].front() == '!')
                    Tag = new SimTag(components[0], '!', theTagsStorage[Tag1Id], nullptr, true, false);
                else
                    Tag = new SimTag(components[0], '#', theTagsStorage[Tag1Id], nullptr, false, false);
                theTagsStorage.push_back(Tag);
                return theTagsStorage.size()-1;
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

            int idx = findTag(theTagsStorage,components[0]);

            if (idx == -1) // New unknow Tag, add it
            {
                SimTag* Tag= new SimTag(components[0], components[1][0], theTagsStorage[Tag1Id], theTagsStorage[Tag2Id], inv1, inv2);
                theTagsStorage.push_back(Tag);
                return theTagsStorage.size()-1;
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
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() - TimeStart;
    }

    double Random(int Depth)
    {
        return rand() / (double)RAND_MAX;
    }

    double Sin(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);
        if (funcPeriode == 0) return 0;
        int timeMSeconds = GetMsSimulation() - funcShift * 1000;
        int periodeMs = funcPeriode * 1000;
        int position = timeMSeconds % periodeMs;

        double angle = (2.0 * 3.14159265358979323846 * position) / periodeMs;
        return (sin(angle)+1)/2;
    }
    double Square(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);
        if (funcPeriode == 0) return 0;
        if (funcPeriode == 0) return 0;
        int timeMSeconds = GetMsSimulation() - funcShift * 1000;
        int periodeMs = funcPeriodeTag * 1000;

        if ((timeMSeconds % periodeMs) < (periodeMs / 2))
            return 1;
        else
            return 0;

    }
    double Triangle(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth + 1);
        if (funcPeriode == 0) return 0;
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);

        int timeMSeconds = GetMsSimulation() - funcShift * 1000;
        int periodeMs = funcPeriode * 1000;
        int position = timeMSeconds % periodeMs;

        int Half_periodeMs = periodeMs / 2.0;
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
        if (funcPeriode == 0) return 0;
        double funcShift = theTagsStorage[funcShiftTag]->GetVal(Depth + 1);

        int timeMSeconds = GetMsSimulation() - funcShift * 1000;
        int periodeMs = funcPeriode * 1000;
        int position = timeMSeconds % periodeMs;
        return (double)position / (double)periodeMs;
    }
    double UserSignal(int Depth)
    {
        double funcPeriode = theTagsStorage[funcPeriodeTag]->GetVal(Depth+1);
        if ((funcPeriode == 0) || (UserfuncValueTags.size() == 0)) return 0;
        double funcShift = theTagsStorage[funcShiftTag]->GetVal();

        int timeMSeconds = GetMsSimulation() - funcShift * 1000;
        int periodeMs = funcPeriodeTag * 1000;
        int position = timeMSeconds % periodeMs;
        int Idx = ((double)position / (double)periodeMs * UserfuncValueTags.size());
        return theTagsStorage[UserfuncValueTags.at(Idx)]->GetVal(Depth + 1);
    }

    int extractFuncParamFromString(const std::string& params) 
    {
        int Idx = 0;
        
        std::string input = params;

        if (input.front() == '(')
            input = input.substr(1);
        if (input.back() == ')')
            input = input.substr(0, input.size() - 1);

        // extract param
        size_t pos = 0;
        while (true) 
        {
            size_t comma = input.find(',', pos);
            std::string param = (comma == std::string::npos)
                ? input.substr(pos)
                : input.substr(pos, comma - pos);

            if (!param.empty()) {
                switch (Idx++)
                {
                    case 0: funcPeriodeTag = GetOrCreateTag(param); break;
                    case 1: funcShiftTag = GetOrCreateTag(param);  break;
                    case 2: funcMagnitudeTag = GetOrCreateTag(param);  break;
                    case 3: funcFloorTag = GetOrCreateTag(param); break;
                    default : UserfuncValueTags.push_back(GetOrCreateTag(param)); // will be clear if it's not the User function
                }
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }

        return Idx;
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

    static std::vector<std::string> parseMathOperation(const std::string& op) 
    {
        std::vector<std::string> components;

        // remove space chars if not already done
        std::string operation = op;
        operation.erase(std::remove(operation.begin(), operation.end(), ' '), operation.end());

        // Find the = symbol 
        size_t assignPos = operation.find('=');
        if (assignPos == std::string::npos) 
        {
            components.push_back(operation);
            return components; 
        }

        // get affected variable (before = symbol)
        std::string leftVar = operation.substr(0, assignPos);

        if (!leftVar.empty())
            components.push_back(leftVar);
        else
            components.push_back("@@"); // anonymous variable

        // get the mathematical operation (after = symbol)
        std::string rightPart = operation.substr(assignPos + 1);

        // find the operator position
        size_t opPos = std::string::npos;
        for (size_t i = 0; i < rightPart.size(); ++i) 
        {
            if (rightPart[i] == '+' || rightPart[i] == '-' || rightPart[i] == '*' || rightPart[i] == '/' 
                || rightPart[i] == '^' || rightPart[i] == '&' || rightPart[i] == '|')
            {
                opPos = i;
                break;
            }
        }

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
std::vector<SimTag*> SimTag::theTagsStorage;

int UserdbTagCount = 0; // Used to count the number of register/unregister calls


///////////////////////////////////////////////////////////
// Begin of the interface functions similar to libplctag //
///////////////////////////////////////////////////////////
int Simulator_tag_create(const char* MathOperation)
{
    if (UserdbTagCount++ <= 0)
        SimTag::SimulationStart();

    return SimTag::GetOrCreateTags(MathOperation);

}
int Simulator_tag_destroy(int identifier)
{
    UserdbTagCount--;
    // Not needed : small quantity, short duration, check dependencies required, identifier is index ... so no
    // only (external) tag usage count to reset the simulation time
    // could be done with an usage counter associate to every Tag
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
    return SimTag::Simulator_writeTag(identifier, val);
}

int64_t Simulator_tag_get_int64(int identifier)
{
    return (int64_t)SimTag::Simulator_readTag(identifier);
}

int Simulator_tag_set_int64(int identifier, int64_t val)
{
    return SimTag::Simulator_writeTag(identifier, val);
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