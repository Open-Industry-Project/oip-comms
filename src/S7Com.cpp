// S7Com for OIP, S7_1200 & S7_1500
// interface similar to libplctag for a simple integration
// F Chaxel 2026

// Basically : 
// x objects S7Plc -> x polling threads, one for each Plc connection.
// The thread is getting I, M, Q adress spaces (S7MemoryBlock) in an infinite loop,
// only the necessary quantity of bytes per spaces, but in one continuous block from the 
// lowest requested @ to the highest @ even when holes exists inside.
// Tags (S7Tag) such as MW5, QD25 are in a list an can be read/write
// using funtions similar to libplctag.
// User reads are done in S7MemoryBlock. Writes generates additional tcp activities.

// Socket Windows & Linux adaptation
#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    inline void socket_close(socket_t s) { closesocket(s); }
    inline int tcp_send(socket_t s, const void* data, int len) { return ::send(s, (const char*)data, len, 0); }
    inline int tcp_recv(socket_t s, void* buf, int len) { return ::recv(s, (char*)buf, len, 0); }
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
    using socket_t = int;
    inline void socket_close(socket_t s) { ::close(s); }
    inline int tcp_send(socket_t s, const void* data, int len) { return (int)::send(s, data, (size_t)len, 0); }
    inline int tcp_recv(socket_t s, void* buf, int len) { return (int)::recv(s, buf, (size_t)len, 0); }
    static constexpr socket_t invalid_socket_v = -1;
#endif

#include <vector>
#include <string>
#include <cstring>
#include <stdint.h>
#include <thread>
#include <chrono>
#include <mutex>

#include "S7Com.hpp"

class S7MemoryBlock 
{
public:
    std::mutex lockResize; // multithread access to the data block
    int Start;
    int Quantity;
    bool Resized;
    std::vector<uint8_t> MemImage;

    S7MemoryBlock()
    {
        Start = 65536;
        Quantity = 0;
        Resized = false;
    }
};

class S7Tag {
    static int IdCounter;
public:
    int Idx;
    int8_t BitIdx;
    uint8_t ByteQty;
    S7MemoryBlock* Mem;
    int Identifier; // 0 to 0xFFFFFF

    S7Tag()
    {
        Identifier = IdCounter++;
        IdCounter &= 0xFFFFFF;
        Idx = 0;
        BitIdx = -1;
        ByteQty = 0;
        Mem = nullptr;
    }
};
int S7Tag::IdCounter = 0;

class S7Plc {

    static uint8_t ConnectRequest[22];
    static uint8_t NegotiatePduLength[25];
    static uint8_t Read_IQM_Bytes[31];
    static uint8_t Write_IQM_Bytes[35];

    uint8_t Buff[1000] = { 0 }; // shared by all methods, beware of concurrency issues
    S7MemoryBlock I, Q, M;
    int PDUMaxLength; // 240 minimum, 960 maximum
    socket_t sock;
    int BackoffTmr;

    int state; // 0 not connected, 1 connected, 2 negotiated and ready to S7_read/S7_write
    int pollingRate;
    std::mutex lockObj; // For socket & buffer sharing between polling thread and S7_write method
    static uint8_t IdCounter;
    std::vector<S7Tag*> Tags;
    std::thread PollingThread;
    bool TerminateThread; // Could be done better, but here it's OK

public:
    uint8_t Identifier;
    std::string PlcHostName;

    S7Plc(const std::string& host, int rate = 10) : PlcHostName(host), pollingRate(rate) 
    {
        Identifier = (IdCounter+1)&0x7F; // 127 devices
        PDUMaxLength = 960;
        BackoffTmr = 100;
        state = 0;
        sock = invalid_socket_v;
        TerminateThread = false;
        PollingThread = std::thread(&S7Plc::s7_poll, this);
    }

    ~S7Plc()
    {
        TerminateThread = true;
        PollingThread.join();
    }

private:
    S7Tag* DecodeTag(const std::string& TagName) 
    {
        S7Tag* tag = new S7Tag();
        if (TagName.length() < 2) return tag;
        char t1 = toupper(TagName[1]);
        if (t1 == 'B') { tag->Idx = std::stoi(TagName.substr(2)); tag->ByteQty = 1; }       // IB0, QB0, MB0
        else if (t1 == 'W') { tag->Idx = std::stoi(TagName.substr(2)); tag->ByteQty = 2; }  // IW0, QW0, MW0
        else if (t1 == 'D') { tag->Idx = std::stoi(TagName.substr(2)); tag->ByteQty = 4; }  // ID0, QD0, MD0
        else if (t1 == 'L') { tag->Idx = std::stoi(TagName.substr(2)); tag->ByteQty = 8; }  // Not a standard notation, not exists in S7, but useful for 64 bits values
        else if (TagName.find('.') != std::string::npos) {
            size_t dot = TagName.find('.');
            tag->Idx = std::stoi(TagName.substr(1, dot - 1)); // X4.0 -> 4
            tag->BitIdx = std::stoi(TagName.substr(dot + 1)); // X4.0 -> 0
            tag->ByteQty = 1;
        }
        else return tag; // return it with Mem not set rather than return null

        char t0 = toupper(TagName[0]);
        if (t0 == 'I' || t0 == 'E') tag->Mem = &I;
        else if (t0 == 'Q' || t0 == 'A') tag->Mem = &Q;
        else if (t0 == 'M') tag->Mem = &M;
        return tag;
    }

    int S7_sockClose()
    {
        socket_close(sock);
        sock = invalid_socket_v;
        state = 0; 
        return 0;
    }
    int S7_TcpExchange(uint8_t* Buffer, int Length) 
    {
        int sent = tcp_send(sock, (const char*)Buffer, Length);
        if (sent != Length) { return S7_sockClose(); }
        int ExpectedSize = 0, Size = 0;

        // we are waiting for the header, which is 4 bytes long or more
        int s = tcp_recv(sock, (char*)Buff + Size, sizeof(Buff));
        if (s < 4) { return S7_sockClose(); }
        Size += s;
        
        if (Buff[0] != 3) { return 0; }
        ExpectedSize = (Buff[2] << 8) + Buff[3];  // Total size is in the header, at position 2 and 3*
        if  (ExpectedSize > sizeof(Buff)) { return 0; } // sanity check, should not happen
        while (Size < ExpectedSize) {
            int s = tcp_recv(sock, (char*)Buff + Size, sizeof(Buff) - Size);
            if (s <= 0) { return S7_sockClose(); }
            Size += s;
        }

        return Size;
    }

    void S7_read(S7MemoryBlock& memory, int start, int quantity) 
    {
        // here quantity is always <= PDUMaxLenght - 25

        lockObj.lock(); // Tcp socket & Buffer are shared, beware of concurrency issues

        Read_IQM_Bytes[23] = (uint8_t)(quantity / 256); // Quantity in bytes
        Read_IQM_Bytes[24] = (uint8_t)(quantity % 256);
        Read_IQM_Bytes[27] = (&memory == &I) ? 0x81 : (&memory == &Q) ? 0x82 : 0x83; // Memory code
        // Start address in bits, for instance 128*8 = 1024 bits = 128 bytes
        Read_IQM_Bytes[28] = (uint8_t)((start * 8) / 65536);    // Start address in bits
        Read_IQM_Bytes[29] = (uint8_t)((start * 8) / 256);      // Start address in bits
        Read_IQM_Bytes[30] = (uint8_t)((start * 8) % 256);      // Start address in bits
        int ret = S7_TcpExchange(Read_IQM_Bytes, sizeof(Read_IQM_Bytes));
        if (ret == 25 + quantity && Buff[21] == 0xFF) 
            std::copy(Buff + 25, Buff + 25 + quantity, memory.MemImage.begin() + (start - memory.Start));
        lockObj.unlock();
    }

    void S7_read(S7MemoryBlock& mem) 
    {
        if (mem.Quantity == 0) return;

        int Quantity, Start; // local copy, can be changed in another thread
        mem.lockResize.lock(); 

        if (mem.Resized) {
            mem.MemImage.resize(mem.Quantity); // always resized with more quantity
            mem.Resized = false;
        }
        Quantity = mem.Quantity;
        Start = mem.Start;
        mem.lockResize.unlock();

        int quantityToRead = (((PDUMaxLength - 25) < (Quantity)) ? (PDUMaxLength - 25) : (Quantity));
        int i = Start;
        while (quantityToRead != 0) {
            S7_read(mem, i, quantityToRead);
            i += quantityToRead;
            quantityToRead = (((PDUMaxLength - 25) < (Quantity - i + Start)) ? (PDUMaxLength - 25) : (Quantity - i + Start));
        }
    }

    void s7_poll() 
    {
        while (true) 
        {

            if (state == 0) {
                if (sock == invalid_socket_v)
                    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

                sockaddr_in serv_addr = {};
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(102);

                int rc = inet_pton(AF_INET, PlcHostName.c_str(), &serv_addr.sin_addr.s_addr);// serv_addr.sin_addr.s_addr = inet_addr(PlcHostName.c_str());
                
                if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
                    BackoffTmr = 100;
                    state = 1;
                }
            }

            if (state == 1) {
                int ret = S7_TcpExchange(ConnectRequest, sizeof(ConnectRequest));
                if (ret == 22) {
                    ret = S7_TcpExchange(NegotiatePduLength, sizeof(NegotiatePduLength));
                    if (ret == 27 && Buff[16] == 0) {
                        PDUMaxLength = (Buff[25] << 8) + Buff[26];
                        state = 2;
                    }
                }
            }

            if (state == 2) {
                S7_read(I);
                S7_read(Q);
                S7_read(M);
                std::this_thread::sleep_for(std::chrono::milliseconds(pollingRate));
            }
            else {
                // much more linked to the tcp stack than this Backoff Timer
                std::this_thread::sleep_for(std::chrono::milliseconds(BackoffTmr));
                BackoffTmr *= 2; if (BackoffTmr > 1600) BackoffTmr = 1600;
            }

            if (TerminateThread) {
                if (sock != invalid_socket_v)
                {
                    lockObj.lock();
                    S7_sockClose();
                    lockObj.unlock();
                }
                return;
            }
        }
    }

public:
    int s7_register_tag(const char* TagName)
    {
        S7Tag* tag = DecodeTag(TagName);
        if (tag->Mem == nullptr) { delete tag;  return -1; }
        if (tag->Idx + tag->ByteQty > 65536) { delete tag;  return -1; }

        tag->Mem->lockResize.lock();
        if (tag->Idx < tag->Mem->Start) { tag->Mem->Start = tag->Idx; tag->Mem->Resized = true; }
        if (tag->Idx + tag->ByteQty > tag->Mem->Start + tag->Mem->Quantity) {
            tag->Mem->Quantity = tag->Idx + tag->ByteQty - tag->Mem->Start;
            tag->Mem->Resized = true;
        }
        tag->Mem->lockResize.unlock();

        Tags.push_back(tag);
        return ((Identifier << 24) | tag->Identifier);
    }

    bool s7_unregister_tag(int identifier)
    {
        // Do not resize the associated memory block
        // Complex things due to overlap variables, so just remove
        for (auto& tag : Tags) {
            if (tag->Identifier == (identifier & 0xFFFFFF))
            {
                std::vector<S7Tag*>::iterator toErase;
                toErase = std::find(Tags.begin(), Tags.end(), tag);
                delete tag;
                Tags.erase(toErase);
                return true;
            }
        }
        return false;
    }

    bool s7_kill_if_empty()
    {
        if (Tags.empty())
        {
            TerminateThread = true;
            return true;
        }
        return false;
    }

    int S7_write(int identifier, uint64_t Value) 
    {
        for (auto& tag : Tags) {
            if (tag->Identifier == (identifier & 0xFFFFFF))
                return S7_write(*tag, Value);
        }
        return -1;
    }
    int S7_write(char *tagName, uint64_t Value) // without registration, not used here
    {
        S7Tag* tag = DecodeTag(tagName);
        if (tag->Mem == nullptr) { delete tag;  return -1; }
        if (tag->Idx + tag->ByteQty > 65536) { delete tag;  return -1; }

        int ret= S7_write(*tag, Value);
        delete tag;
        return ret;
    }
    int S7_write(const S7Tag& tag, uint64_t Value) 
    {
        if (state != 2 || !tag.Mem) return -1;
        lockObj.lock();// Tcp socket & Buffer are shared, beware of concurrency issues
        memcpy(Buff, Write_IQM_Bytes, sizeof(Write_IQM_Bytes));
        Buff[3] = (uint8_t)(sizeof(Write_IQM_Bytes) + tag.ByteQty);
        Buff[16] = (uint8_t)(Buff[16] + tag.ByteQty - 1);
        Buff[27] = (tag.Mem == &I) ? 0x81 : (tag.Mem == &Q) ? 0x82 : 0x83;
        Buff[28] = (uint8_t)((tag.Idx * 8) / 65536);
        Buff[29] = (uint8_t)((tag.Idx * 8) / 256);
        Buff[30] = (uint8_t)((tag.Idx * 8) % 256);
        if (tag.BitIdx != -1) {
            Buff[22] = 1;
            Buff[30] = (uint8_t)(Buff[30] | tag.BitIdx);
            Buff[32] = 3; // Write bit
            // Buff[32] = 4; Write Byte, value already done in the original frame
            // quantity is also already adjusted for one bit writing

        }
        else {
            Buff[24] = (uint8_t)(tag.ByteQty);
            Buff[34] = (uint8_t)(tag.ByteQty << 3);
        }
        // S7 are Big - Endian
        uint8_t ValueBytes[8];
        for (int i = 0; i < 8; ++i) ValueBytes[i] = (Value >> (8 * (7 - i))) & 0xFF;

        memcpy(Buff + 35, ValueBytes + (8 - tag.ByteQty), tag.ByteQty);
        int ret = S7_TcpExchange(Buff, sizeof(Write_IQM_Bytes) + tag.ByteQty);
        uint8_t ErrorCode = Buff[21];
        lockObj.unlock();
        if (ret == 22 && ErrorCode == 0xFF) return 0; else return -1;
    }

    uint64_t S7_read(int identifier) {
        for (auto& tag : Tags) {
            if (tag->Identifier == (identifier & 0xFFFFFF))
                return S7_read(*tag);
        }
        return 0;
    }

    uint64_t S7_read(const S7Tag& tag) {
        if (state != 2 || !tag.Mem) return 0;
        tag.Mem->lockResize.lock();
        if ((tag.Mem->Resized == true) || (tag.Mem->Start > tag.Idx || tag.Mem->Start + tag.Mem->Quantity < tag.Idx + tag.ByteQty)) {
            tag.Mem->lockResize.unlock();
            return 0;
        }
        std::vector<uint8_t> b(tag.Mem->MemImage.begin() + (tag.Idx - tag.Mem->Start),
            tag.Mem->MemImage.begin() + (tag.Idx - tag.Mem->Start) + tag.ByteQty);
        std::reverse(b.begin(), b.end());
        tag.Mem->lockResize.unlock();
        uint64_t Value = 0;
        for (size_t i = 0; i < b.size(); ++i) Value |= ((uint64_t)b[i]) << (8 * i);
        if (tag.BitIdx != -1) Value = (Value >> tag.BitIdx) & 1;
        return Value;
    }
};

// S7_1200 & S7_1500
uint8_t S7Plc::ConnectRequest[22] = {
    0x03,0x00,0x00,0x16,0x11,0xe0,0x00,0x00,0x00,0x01,0x00,0xc0,0x01,0x0a,0xc1,0x02,
    0x4b,0x54,0xc2,0x02,0x03,0x01
};
// 960 Bytes PDU Length
uint8_t S7Plc::NegotiatePduLength[25] = {
    0x03,0x00,0x00,0x19,0x02,0xf0,0x80,0x32,0x01,0x00,0x00,0x00,0x05,0x00,0x08,0x00,
    0x00,0xf0,0x00,0x00,0x01,0x00,0x01,0x03,0xc0
};
uint8_t S7Plc::Read_IQM_Bytes[31] = {
    0x03,0x00,0x00,0x1f,0x02,0xf0,0x80,0x32,0x01,0x00,0x00,0x00,0x09,0x00,0x0e,0x00,
    0x00,0x04,0x01,0x12,0x0a,0x10,0x02,0x00,128,0x00,0x00,0x83,0x00,0x00,0x00
};
uint8_t S7Plc::Write_IQM_Bytes[35] = {
    0x03,0x00,0x00,0x23,0x02,0xf0,0x80,0x32,0x01,0x00,0x00,0x01,0x6f,0x00,0x0e,0x00,
    0x05,0x05,0x01,0x12,0x0a,0x10,0x02,0x00,0x01,0x00,0x00,0x82,0x00,0x00,0x00,0x00,
    0x04,0x00,0x01
};
uint8_t S7Plc::IdCounter = 0;

std::vector<S7Plc*> S7Plcs;
bool S7_WSAStartupdone = false;

uint64_t S7_readTag(int identifier)
{
    for (auto plc : S7Plcs) {
        if (plc->Identifier == (identifier >> 24))
            return plc->S7_read(identifier);
    }
    return (uint64_t)0;
};

int S7_writeTag(int identifier, uint64_t Value)
{
    for (auto plc : S7Plcs) {
        if (plc->Identifier == (identifier >> 24))
            return plc->S7_write(identifier, Value);
    }
    return -1;
};

///////////////////////////////////////////////////////////
// Begin of the interface functions similar to libplctag //
///////////////////////////////////////////////////////////

int S7_tag_create(const char* Hostname, const char* TagName)
{
    if (S7_WSAStartupdone == false)
    {
        #if defined(_WIN32)
            WSADATA wsaData;
            std::ignore = WSAStartup(MAKEWORD(2, 2), &wsaData);
        #endif
        S7_WSAStartupdone = true;
    }

    for (auto plc : S7Plcs) {
        if (plc->PlcHostName == Hostname)
            return plc->s7_register_tag(TagName);
    }
    S7Plc* plcnew = new S7Plc(Hostname, 20);
    S7Plcs.push_back(plcnew);
    return plcnew->s7_register_tag(TagName);
};

int S7_tag_destroy(int identifier)
{
    for (auto it = S7Plcs.begin(); it != S7Plcs.end(); ++it) {
        if ((*it)->Identifier == (identifier >> 24)) {
            (*it)->s7_unregister_tag(identifier);
            if ((*it)->s7_kill_if_empty()) {
                delete* it;
                S7Plcs.erase(it);

                if (S7Plcs.empty() == true)
                {
                    #if defined(_WIN32)
                        WSACleanup();
                    #endif
                    S7_WSAStartupdone = false;
                }
            }
            return 0;
        }
    }
    return -1;
};

int S7_tag_write(int identifier)
{
    // Write already done with set.
    for (auto plc : S7Plcs) {
        if (plc->Identifier == (identifier >> 24))
            return 0;
    }
     return -1;
}
int S7_tag_read(int identifier, int timeout)
{
    // Only verify that the PLC is configured for the tag
    for (auto plc : S7Plcs) {
        if (plc->Identifier == (identifier >> 24))
            return 0;
    }
    return -1;
}
int S7_tag_get_bit(int32_t tag)
{
    return (int)S7_readTag(tag);
}
int S7_tag_set_bit(int32_t tag, int val)
{
    return S7_writeTag(tag, val);
}
uint64_t S7_tag_get_uint64(int32_t tag)
{
    return (uint64_t)S7_readTag(tag);
}
int S7_tag_set_uint64(int32_t tag, uint64_t val)
{
    return S7_writeTag(tag, val);
}
int64_t S7_tag_get_int64(int32_t tag)
{
    return (int64_t)S7_readTag(tag);
}
int S7_tag_set_int64(int32_t tag, int64_t val)
{
    return S7_writeTag(tag, (uint64_t) val);
}
uint32_t S7_tag_get_uint32(int32_t tag)
{
    return (uint32_t)S7_readTag(tag);
}
int S7_tag_set_uint32(int32_t tag, uint32_t val)
{
    return S7_writeTag(tag, (uint64_t)val);
}

int32_t S7_tag_get_int32(int32_t tag)
{
    return (int32_t)S7_readTag(tag);
}
int S7_tag_set_int32(int32_t tag, int32_t val)
{
    return S7_writeTag(tag, (uint64_t)val);
}
uint16_t S7_tag_get_uint16(int32_t tag)
{
    return (uint16_t)S7_readTag(tag);
}
int S7_tag_set_uint16(int32_t tag, uint16_t val)
{
    return S7_writeTag(tag, (uint64_t)val);
}

int16_t S7_tag_get_int16(int32_t tag)
{
    return (int16_t)S7_readTag(tag);
}
int S7_tag_set_int16(int32_t tag, int16_t val)
{
    return S7_writeTag(tag, (uint64_t)val);
}

uint8_t S7_tag_get_uint8(int32_t tag)
{
    return (uint8_t)S7_readTag(tag);
}
int S7_tag_set_uint8(int32_t tag, uint8_t val)
{
    return S7_writeTag(tag, (uint64_t)val);
}

int8_t S7_tag_get_int8(int32_t tag)
{ 
    return (int8_t)S7_readTag(tag);
}
int S7_tag_set_int8(int32_t tag, int8_t val)
{
    return S7_writeTag(tag, (uint64_t)val);
}
double S7_tag_get_float64(int32_t tag)
{
    uint64_t Val = S7_readTag(tag);
    double*  d = (double*)Val;
    return *d;

}
int S7_tag_set_float64(int32_t tag, double val)
{
    uint64_t* Val = (uint64_t*)(&val);
    return S7_writeTag(tag, (uint64_t)*Val);
}
float S7_tag_get_float32(int32_t tag)
{
    uint64_t Val = S7_readTag(tag);
    float* d = (float*)Val;
    return *d;
}
int S7_tag_set_float32(int32_t tag, float val)
{
    uint64_t* Val = (uint64_t*)(&val);
    return S7_writeTag(tag, (uint64_t)*Val);
}
