#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>
#include <ns3/ipv4-address.h>

using namespace ns3;
using namespace std;

typedef vector<uint8_t> bytes;

struct LinkProfile {
    double latency;
    double bandwidth;

    LinkProfile() : latency(0.0), bandwidth(0.0) {}
};

struct ClientMemory {
    Ipv4Address my_ip;
    uint16_t my_udp_port;
    uint16_t my_tcp_port;
    vector<bytes> buffer;
    LinkProfile profile;

    uint64_t uploaded_bytes = 0;
    uint64_t downloaded_bytes = 0;
    int integrity_failures = 0;
    int freerider_stack = 0;
};

const int peer_cnt = 8;
ClientMemory arr[1 + peer_cnt];

void InitializeClientMemory(int index, Ipv4Address ip, uint16_t udp_port, uint16_t tcp_port) {
    arr[index].my_ip = ip;
    arr[index].my_udp_port = udp_port;
    arr[index].my_tcp_port = tcp_port;
    arr[index].buffer.clear();
    arr[index].profile = LinkProfile();
    arr[index].uploaded_bytes = 0;
    arr[index].downloaded_bytes = 0;
    arr[index].integrity_failures = 0;
    arr[index].freerider_stack = 0;
}

void UpdateMetrics(int index, uint64_t uploaded, uint64_t downloaded, bool integrityFailed = false) {
    arr[index].uploaded_bytes += uploaded;
    arr[index].downloaded_bytes += downloaded;
    if (integrityFailed) {
        arr[index].integrity_failures++;
    }
}

void RequestChunkFromParent(int index, uint32_t chunkNumber) {
    cout << "Requesting chunk #" << chunkNumber << " from parent for client " << index << "\n";
    string request = "REQUEST:Chunk:" + to_string(chunkNumber);
    SendUdpFromMemory(index, request);
    arr[index].freerider_stack++;
}

void DetectFreeridersAndDelete() {
    cout << "Analyzing clients for freerider behavior and deleting flagged nodes...\n";
    for (int i = 1; i <= peer_cnt; i++) {
        double ratio = arr[i].downloaded_bytes > 0
                           ? (double)arr[i].uploaded_bytes / arr[i].downloaded_bytes
                           : 0.0;
        cout << "Client " << i << ": Uploaded = " << arr[i].uploaded_bytes
             << ", Downloaded = " << arr[i].downloaded_bytes
             << ", Integrity Failures = " << arr[i].integrity_failures
             << ", Freerider Stack = " << arr[i].freerider_stack
             << ", Upload/Download Ratio = " << ratio << "\n";

        if (ratio < 0.1 || arr[i].freerider_stack > 5) {
            cerr << "Client " << i << " flagged as a freerider. Deleting peer.\n";
            deletePeerQuery(arr[i].my_ip.ToString());
            InitializeClientMemory(i, Ipv4Address("0.0.0.0"), 0, 0);
        }
    }
}

void ProcessDataIntegrityResult(int index, const string& content, uint32_t chunkNumber, int parentIndex) {
    cout << "Validating data integrity for client " << index << "\n";
    bool integrityFailed = (content != "valid");
    UpdateMetrics(index, 0, 0, integrityFailed);
    if (!integrityFailed) {
        cout << "Data is valid for client " << index << "\n";
    } else {
        cerr << "Data integrity failed for client " << index << ". Requesting retransmission from parent.\n";
        RequestChunkFromParent(parentIndex, chunkNumber);
    }
    DetectFreeridersAndDelete(); // Trigger freerider detection
}

void ProcessReceivedData(uint8_t senderType, uint32_t chunkNumber, const string& content, int index, int parentIndex) {
    switch (senderType) {
        case 0:
            ProcessDataIntegrityResult(index, content, chunkNumber, parentIndex);
            break;
        case 1:
            ProcessVideoData(index, chunkNumber, content);
            DetectFreeridersAndDelete(); // Trigger freerider detection
            break;
        case 2:
            ProcessIntegrityCheckResult(index, content);
            break;
        case 3:
            ProcessRetransmissionRequest(index, content);
            break;
        default:
            cerr << "Unknown sender type: " << senderType << "\n";
            break;
    }
}

void HandleReceivedTcpData(Ptr<Socket> socket, int index) {
    Ptr<Packet> packet = socket->Recv();
    uint8_t buffer[1029];
    packet->CopyData(buffer, sizeof(buffer));

    uint8_t senderType = buffer[0];
    uint32_t chunkNumber;
    memcpy(&chunkNumber, buffer + 1, sizeof(chunkNumber));
    chunkNumber = ntohl(chunkNumber);
    string content(reinterpret_cast<char*>(buffer + 5), 1024);

    int parentIndex = index - 1; // Example: Determine parentIndex
    ProcessReceivedData(senderType, chunkNumber, content, index, parentIndex);
}

void HandleReceivedUdpData(Ptr<Socket> socket, int index) {
    Ptr<Packet> packet = socket->Recv();
    uint8_t buffer[1024];
    packet->CopyData(buffer, sizeof(buffer));

    string content(reinterpret_cast<char*>(buffer), 1024);
    cout << "Received UDP data for client " << index << ": " << content << "\n";
    DetectFreeridersAndDelete(); // Trigger freerider detection
}

int main() {
    for (int i = 1; i <= peer_cnt; i++) {
        InitializeClientMemory(i, Ipv4Address("192.168.1." + to_string(i)), 5000 + i, 6000 + i);
    }

    // Simulate integrity failure and request
    ProcessDataIntegrityResult(1, "invalid", 42, 2);

    // Simulate receiving TCP data
    // Example: Ptr<Socket> tcpSocket; HandleReceivedTcpData(tcpSocket, 1);

    // Simulate receiving UDP data
    // Example: Ptr<Socket> udpSocket; HandleReceivedUdpData(udpSocket, 1);

    return 0;
}
