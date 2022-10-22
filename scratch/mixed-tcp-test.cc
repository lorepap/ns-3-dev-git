// Testing TCP and DCTCP in congestion scenario

#include <iomanip>
#include <iostream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

// to_string function patch
namespace patch
{
    template < typename T > std::string to_string( const T& n )
    {
        std::ostringstream stm ;
        stm << n ;
        return stm.str() ;
    }
}


NS_LOG_COMPONENT_DEFINE ("CongestionControlTest");

std::string dir = "simulations/";

// Global auxiliary variables
bool firstCwnd1 = true;
bool firstSshThr1 = true;
uint32_t cWndValue1;
uint32_t ssThreshValue1;
bool firstCwnd2 = true;
bool firstSshThr2 = true;
uint32_t cWndValue2;
uint32_t ssThreshValue2;
// bool firstCwnd3 = true;
// bool firstSshThr3 = true;
// uint32_t cWndValue3;
// uint32_t ssThreshValue3;
bool firstRtt = true;


// Global streams
Ptr<OutputStreamWrapper> cWndStream1;
Ptr<OutputStreamWrapper> cWndStream2;
Ptr<OutputStreamWrapper> cWndStream3;
Ptr<OutputStreamWrapper> ssThreshStream1;
Ptr<OutputStreamWrapper> ssThreshStream2;
Ptr<OutputStreamWrapper> ssThreshStream3;
Ptr<OutputStreamWrapper> t1QueueStream;
Ptr<OutputStreamWrapper> rttStream;


uint32_t checkTimes;
double avgQueueSize;
std::stringstream filePlotQueue;
std::stringstream filePlotQueueAvg;


// Set function for CC algorithms
void
setDctcp ()
{
  NS_LOG_INFO ("Enabling DCTCP");
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpDctcp"));
  // Set default parameters for RED queue disc
  Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
  // ARED may be used but the queueing delays will increase; it is disabled
  // here because the SIGCOMM paper did not mention it
  // Config::SetDefault ("ns3::RedQueueDisc::ARED", BooleanValue (true));
  // Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (true));
  Config::SetDefault("ns3::RedQueueDisc::UseHardDrop", BooleanValue(false));
  Config::SetDefault("ns3::RedQueueDisc::MeanPktSize", UintegerValue(1500));
  // Triumph and Scorpion switches used in DCTCP Paper have 4 MB of buffer
  // If every packet is 1500 bytes, 2666 packets can be stored in 4 MB
  Config::SetDefault("ns3::RedQueueDisc::MaxSize", QueueSizeValue(QueueSize("2666p")));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault("ns3::RedQueueDisc::QW", DoubleValue(1));
  Config::SetDefault("ns3::RedQueueDisc::MinTh", DoubleValue(20));
}

void
setTcp () {
  NS_LOG_INFO ("Enabling Tcp New Reno");
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
  Config::SetDefault ("ns3::TcpSocketBase::UseEcn", EnumValue(TcpSocketState::Off));
}

//---------- Tracing functions --------------
void
PrintProgress (Time interval)
{
  std::cout << "Progress to " << std::fixed << std::setprecision (1) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
  Simulator::Schedule (interval, &PrintProgress, interval);
}

void
DevicePacketsInQueueTrace (uint32_t oldValue, uint32_t newValue)
{
  std::cout << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " DevicePacketsInQueue " << oldValue << " to " << newValue << std::endl;
}

void
DeviceBytesTrace (uint32_t oldValue, uint32_t newValue)
{
  std::cout << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " ReceivedBytes " << oldValue << " to " << newValue << std::endl;
}

// void
// DeviceEnqueue (Ptr<Queue>::Enqueue)
// {
//   std::cout << Simulator::Now ().GetSeconds () << "Enqueue: " << Enqueue->GetStats() << std::endl;
// }

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
TraceRtt (std::string rtt_tr_file_name, uint32_t nodeid)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/" + patch::to_string(nodeid) + "/$ns3::TcpL4Protocol/SocketList/*/RTT", MakeCallback (&RttTracer));
}

void
CheckQueueSize (Ptr<QueueDisc> queue)
{
    uint32_t qSize = queue->GetCurrentSize().GetValue();

    avgQueueSize += qSize;
    checkTimes++;

    // check queue size every 1/100 of a second
    Simulator::Schedule(Seconds(0.01), &CheckQueueSize, queue);

    std::ofstream fPlotQueue(filePlotQueue.str().c_str(), std::ios::out | std::ios::app);
    fPlotQueue << Simulator::Now().GetSeconds() << " " << qSize << std::endl;
    fPlotQueue.close();

    std::ofstream fPlotQueueAvg(filePlotQueueAvg.str().c_str(), std::ios::out | std::ios::app);
    fPlotQueueAvg << Simulator::Now().GetSeconds() << " " << avgQueueSize / checkTimes << std::endl;
    fPlotQueueAvg.close();
}

void
CheckT1QueueSize (ns3::Ptr<ns3::OutputStreamWrapper> t1QueueStream, std::string path, Ptr<QueueDisc> queue)
{
  // 1500 byte packets
  uint32_t qSize = queue->GetNPackets ();
  Time backlog = Seconds (static_cast<double> (qSize * 1500 * 8) / 1e10); // 10 Gb/s
  // report size in units of packets and ms
  *t1QueueStream->GetStream() << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
  // check queue size every 1/100 of a second
  Simulator::Schedule (MilliSeconds (10), &CheckT1QueueSize, t1QueueStream, path, queue);
}

std::string get_timestamp()
{
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];

    time (&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer,80,"%d-%m-%Y-%H-%M-%S",timeinfo);
    return std::string(buffer);
}

static void
CwndTracer1 (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd1)
    {
      *cWndStream1->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd1 = false;
    }
  *cWndStream1->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue1 = newval;

  if (!firstSshThr1)
    {
      *ssThreshStream1->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue1 << std::endl;
    }
}

static void
CwndTracer2 (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd2)
    {
      *cWndStream2->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd2 = false;
    }
  *cWndStream2->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue2 = newval;

  if (!firstSshThr2)
    {
      *ssThreshStream2->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue2 << std::endl;
    }
}

// static void
// CwndTracer3 (uint32_t oldval, uint32_t newval)
// {
//   if (firstCwnd3)
//     {
//       *cWndStream3->GetStream () << "0.0 " << oldval << std::endl;
//       firstCwnd3 = false;
//     }
//   *cWndStream3->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
//   cWndValue3 = newval;

//   if (!firstSshThr3)
//     {
//       *ssThreshStream3->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue3 << std::endl;
//     }
// }


static void
TraceCwnd1 (std::string cwnd_file_name, uint32_t nodeid)
{
  AsciiTraceHelper ascii;
  cWndStream1 = ascii.CreateFileStream ((cwnd_file_name).c_str ());
  std::ostringstream stream;
  stream << "/NodeList/" << patch::to_string(nodeid) << "/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow";
  std::cout << stream.str() << std::endl;
  Config::ConnectWithoutContext (stream.str(), MakeCallback (&CwndTracer1));
}

static void
TraceCwnd2 (std::string cwnd_file_name, uint32_t nodeid)
{
  AsciiTraceHelper ascii;
  cWndStream2 = ascii.CreateFileStream ((cwnd_file_name).c_str ());
  std::ostringstream stream;
  stream << "/NodeList/" << patch::to_string(nodeid) << "/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow";
  std::cout << stream.str() << std::endl;
  Config::ConnectWithoutContext (stream.str(), MakeCallback (&CwndTracer2));
}

// static void
// TraceCwnd3 (std::string cwnd_file_name, uint32_t nodeid)
// {
//   AsciiTraceHelper ascii;
//   cWndStream3 = ascii.CreateFileStream ((cwnd_file_name).c_str ());
//   std::ostringstream stream;
//   stream << "/NodeList/" << patch::to_string(nodeid) << "/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow";
//   std::cout << stream.str() << std::endl;
//   Config::ConnectWithoutContext (stream.str(), MakeCallback (&CwndTracer3));
// }


int main (int argc, char *argv[])
{
    // Simulation parameters
    Time::SetResolution (Time::NS);
    char buffer[80];
    setenv("TZ", "/usr/share/zoneinfo/America/Chicago", 1);
    time_t rawtime;
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
    std::string currentTime (buffer);
    std::string transportProt = "tcp";  //tcp or dctcp
    std::string simName = transportProt;

    // Data rate
    std::string redLinkDataRate = "1Gbps";
    std::string redLinkDelay = "10us";
    std::string accessLinkDataRate = "1Gbps";
    std::string accessLinkDelay = "10us";
    std::string clientDataRate = "10Gbps";

    // Queue parameters
    uint32_t MinTh = 50;
    uint32_t MaxTh = 150;
    uint32_t K = 65; // DCTCP -> MinTh = MaxTh = K

    bool enableSwitchEcn = true;
    // uint32_t meanPktSize = 500;
    // uint32_t maxBytes = 0; // No limit
    Time startTime = Seconds(0);
    Time stopTime = Seconds(5);

    bool printRedStats = true;
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("protocol", "ns-3 TCP protocol", transportProt);
    cmd.AddValue("stoptime", "Simulation Stop Time", stopTime);
    cmd.AddValue("filename", "Simulation Stop Time", simName);
    cmd.Parse(argc, argv);

    NS_LOG_INFO ("Create nodes.");
    NodeContainer c1, c2;
    NodeContainer r;
    uint32_t nc1, nc2, nr;
    nc1 = 2;
    nc2 = 2;
    nr = nc1+nc2; // each sender with a receiver
    // clients
    c1.Create(nc1);
    c2.Create(nc2);
    // servers
    r.Create(nr);
    // switches
    Ptr<Node> s0 = CreateObject<Node>();
    Ptr<Node> s1 = CreateObject<Node>();

    // NodeContainer n0n0 = NodeContainer (c.Get (0), c.Get (2));
    // NodeContainer n1n2 = NodeContainer (c.Get (1), c.Get (2));
    // NodeContainer n2n3 = NodeContainer (c.Get (2), c.Get (3));
    // NodeContainer n3n4 = NodeContainer (c.Get (3), c.Get (4));
    // NodeContainer n3n5 = NodeContainer (c.Get (3), c.Get (5));

    // Testing 3 senders 3 receivers
    // NodeContainer c0s0 = NodeContainer (c.Get (0), s.Get (0));
    // NodeContainer c1s0 = NodeContainer (c.Get (1), s.Get (0));
    // NodeContainer c2s0 = NodeContainer (c.Get (2), s.Get (0));
    // NodeContainer s0s1 = NodeContainer (s.Get (0), s.Get (1));
    // NodeContainer s1r0 = NodeContainer (s.Get (1), r.Get (0));
    // NodeContainer s1r1 = NodeContainer (s.Get (1), r.Get (1));
    // NodeContainer s1r2 = NodeContainer (s.Get (1), r.Get (2));

    NS_LOG_INFO ("Set default configurations.");
    std::cout << "Protocol: " << transportProt << std::endl;
    
    if (transportProt.compare("dctcp")==0)
    {
      setDctcp();
    }
    else // tcp newreno or mixed
    {
      setTcp();
    }

    Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (80)));

    NS_LOG_INFO ("Create channels.");
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(accessLinkDataRate));
    p2p.SetChannelAttribute("Delay", StringValue(accessLinkDelay));

    PointToPointHelper p2pRouter;
    p2pRouter.SetDeviceAttribute("DataRate", StringValue(redLinkDataRate));
    p2pRouter.SetChannelAttribute("Delay", StringValue(redLinkDelay));

    std::vector<NetDeviceContainer> devc1s0;
    devc1s0.reserve(nc1);
    std::vector<NetDeviceContainer> devc2s0;
    devc2s0.reserve(nc2);
    std::vector<NetDeviceContainer> devs1r;
    devs1r.reserve(nr);

    NetDeviceContainer devs0s1 = p2pRouter.Install(s0, s1);

    for (std::size_t i = 0; i < nc1; i++)
    {
        Ptr<Node> n = c1.Get(i);
        devc1s0.push_back(p2p.Install(n, s0));
    }
    for (std::size_t i = 0; i < nc2; i++)
    {
        Ptr<Node> n = c2.Get(i);
        devc2s0.push_back(p2p.Install(n, s0));
    }
    for (std::size_t i = 0; i < nr; i++)
    {
        Ptr<Node> n = r.Get(i);
        devs1r.push_back(p2p.Install(s1, n));
    }

    NS_LOG_INFO ("Install internet stack.");
    InternetStackHelper internet;

    if (transportProt.compare("mixed")==0)
    {
      internet.Install(c1); // tcp on c1
      setDctcp();
      internet.Reset();
    
      internet.Install(c2); // dctcp on c2
      internet.Install(r);
      internet.Install(s0);
      internet.Install(s1);
    }
    else
    {
      internet.InstallAll();
    }

    NS_LOG_INFO ("Install RED for bottle-neck path.");
    TrafficControlHelper tchRedBottleneck;

    // Set red queue with minth=maxth=K when dctcp is active
    if ((transportProt.compare("dctcp")==0) || (transportProt.compare("mixed")==0))
    {
      tchRedBottleneck.SetRootQueueDisc("ns3::RedQueueDisc",
      "MinTh", DoubleValue(K),
      "MaxTh", DoubleValue(K));

    // MinTh = 50, MaxTh = 150 recommended in ACM SIGCOMM 2010 DCTCP Paper
    // This yields a target (MinTh) queue depth of 60us at 10 Gb/s
    }
    else
    {
      tchRedBottleneck.SetRootQueueDisc("ns3::RedQueueDisc",
                              "LinkBandwidth",
                              StringValue("10Gbps"),
                              "LinkDelay",
                              StringValue("10us"),
                              "MinTh",
                              DoubleValue(MinTh),
                              "MaxTh",
                              DoubleValue(MaxTh));
    }
    QueueDiscContainer queueDiscs = tchRedBottleneck.Install(devs0s1);

    // TODO: Try installing RED also on access paths
    NS_LOG_INFO("Install FIFO for access paths");
    TrafficControlHelper tchPfifo;
    uint16_t handle = tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
    tchPfifo.AddInternalQueues (handle, 3, "ns3::DropTailQueue", "MaxSize", StringValue ("1000p"));

    for (std::size_t i = 0; i < nc1; i++)
    {
        tchPfifo.Install(devc1s0[i].Get(1));
    }
    for (std::size_t i = 0; i < nc2; i++)
    {
        tchPfifo.Install(devc2s0[i].Get(1));
    }
    for (std::size_t i = 0; i < nr; i++)
    {
        tchPfifo.Install(devs1r[i].Get(0));
    }
    // tchPfifo.Install(devc0s0);
    // tchPfifo.Install(devc1s0);
    // tchPfifo.Install(devc2s0);
    // tchPfifo.Install(devs1r0);
    // tchPfifo.Install(devs1r1);
    // tchPfifo.Install(devs1r2);

    NS_LOG_INFO("Assign IP Address.");
    Ipv4AddressHelper address;
    std::vector<Ipv4InterfaceContainer> ipc1s0;
    ipc1s0.reserve(nc1);
    std::vector<Ipv4InterfaceContainer> ipc2s0;
    ipc2s0.reserve(nc2);
    std::vector<Ipv4InterfaceContainer> ips1r;
    ips1r.reserve(nr);
    address.SetBase("172.16.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ips0s1 = address.Assign(devs0s1);
    address.SetBase("10.1.1.0", "255.255.255.0");
    for (std::size_t i = 0; i < nc1; i++)
    {
        ipc1s0.push_back(address.Assign(devc1s0[i]));
        address.NewNetwork();
    }
    address.SetBase("10.2.1.0", "255.255.255.0");
    for (std::size_t i = 0; i < nc2; i++)
    {
        ipc2s0.push_back(address.Assign(devc2s0[i]));
        address.NewNetwork();
    }
    address.SetBase("10.4.1.0", "255.255.255.0");
    for (std::size_t i = 0; i < nr; i++)
    {
        ips1r.push_back(address.Assign(devs1r[i]));
        address.NewNetwork();
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();


    NS_LOG_INFO("Set up routing.");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    NS_LOG_INFO("Install Applications.");
    
    // Servers
    for (std::size_t i = 0; i < nr; i++)
    {
      uint16_t port = 50000 + i;
      Address sinkLocalAddress(InetSocketAddress(Ipv4Address::GetAny(), port));
      PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkLocalAddress);
      ApplicationContainer sinkApp = sinkHelper.Install(r.Get(i));
      sinkApp.Start(startTime);
      sinkApp.Stop(stopTime);
    }

    // Clients 1 and 2
    for (std::size_t i = 0; i < nc1 + nc2; i++)
    {
      uint16_t port = 50000 + i;
      OnOffHelper clientHelper("ns3::TcpSocketFactory", Address());
      clientHelper.SetAttribute("OnTime",
                                  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
      clientHelper.SetAttribute("OffTime",
                                  StringValue("ns3::ConstantRandomVariable[Constant=0]"));
      clientHelper.SetAttribute("DataRate", DataRateValue(DataRate(clientDataRate)));
      clientHelper.SetAttribute("PacketSize", UintegerValue(1000));
      
      ApplicationContainer clientApps;
      AddressValue remoteAddress(InetSocketAddress(ips1r[i].GetAddress(1), port));
      clientHelper.SetAttribute("Remote", remoteAddress);

      if (i < nc1)
        clientApps.Add(clientHelper.Install(c1.Get(i)));
      else
        clientApps.Add(clientHelper.Install(c2.Get(i-nc1)));
        
      clientApps.Start(startTime);
      clientApps.Stop(stopTime);
    }
    // // Clients-2
    // for (std::size_t i = nc1; i < nc2; i++)
    // {
    //   uint16_t port = 50000 + i;
    //   OnOffHelper clientHelper("ns3::TcpSocketFactory", Address());
    //   clientHelper.SetAttribute("OnTime",
    //                               StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    //   clientHelper.SetAttribute("OffTime",
    //                               StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    //   clientHelper.SetAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    //   clientHelper.SetAttribute("PacketSize", UintegerValue(1000));
    //   ApplicationContainer clientApps;
    //   AddressValue remoteAddress(InetSocketAddress(ips1r[i].GetAddress(1), port));
    //   clientHelper.SetAttribute("Remote", remoteAddress);
    //   clientApps.Add(clientHelper.Install(c2.Get(i)));
    //   clientApps.Start(startTime);
    //   clientApps.Stop(stopTime);
    // }

    Simulator::Schedule (MilliSeconds(1000), &PrintProgress, MilliSeconds(100));
    
  
    ///////////////////////////////// --------- Tracing ------------ //////////////////////////////////////////
    
    dir += (simName + '/' + currentTime + "/");
    std::string dirToSave = "mkdir -p " + dir;
    system (dirToSave.c_str ());
    system ((dirToSave + "/pcap/").c_str ());
    system ((dirToSave + "/cwndTraces/").c_str ());
    system ((dirToSave + "/rttTraces/").c_str ());
    system ((dirToSave + "/queueStats/").c_str ());
    // p2p.EnablePcapAll (dir + "/pcap/N", true);
    p2pRouter.EnablePcap(dir + "/pcap/s", s0->GetDevice(0)); //pcap tracing on congested switch

    // Trace cwnd
    uint32_t id0 = c1.Get(0)->GetId();
    uint32_t id1 = c2.Get(0)->GetId();
    // uint32_t id2 = c.Get(2)->GetId();
    // std::cout << "node " + patch::to_string(id0);
    // std::cout << "node " + patch::to_string(id1);
    // std::cout << "node " + patch::to_string(id2);
    Simulator::Schedule (Seconds(0.01), &TraceCwnd1, dir + "cwndTraces/N1-" + patch::to_string(id0) + "-cwnd" + ".data", id0);
    Simulator::Schedule (Seconds(0.01), &TraceCwnd2, dir + "cwndTraces/N2-" + patch::to_string(id1) + "-cwnd" + ".data", id1);
    Simulator::Schedule (Seconds(0.01), &TraceRtt, dir + "rttTraces/N1-" + patch::to_string(id0) + "-rtt" + ".data", id0);

    // std::string t1QueueFileName;
    // t1QueueFileName = dir + "queueStats/" + "t1QueueLength" + ".data";
    // AsciiTraceHelper asciiT1;
    // t1QueueStream = asciiT1.CreateFileStream (t1QueueFileName.c_str ());
    // CheckT1QueueSize(t1QueueStream, t1QueueFileName, queueDiscs.Get(0));

    // Trace queue on left switch
    filePlotQueue << dir << "/queueStats/" << "red-queue.plotme";
    filePlotQueueAvg << dir << "/queueStats/" << "red-queue_avg.plotme";
    remove (filePlotQueue.str ().c_str ());
    remove (filePlotQueueAvg.str ().c_str ());
    Ptr<QueueDisc> queue = queueDiscs.Get (0);
    Simulator::ScheduleNow (&CheckQueueSize, queue);

    // Ptr<NetDevice> nd = devs0s1.Get (0);
    // Ptr<PointToPointNetDevice> ptpnd = DynamicCast<PointToPointNetDevice> (nd);
    // Ptr<Queue> q = ptpnd->GetQueue ();
    // q->TraceConnectWithoutContext ("PacketsInQueue", MakeCallback (&DevicePacketsInQueueTrace));
    // q->TraceConnectWithoutContext("BytesInQueue", MakeCallback (&DeviceBytesTrace));

    NS_LOG_INFO ("Run Simulation");
    Simulator::Stop (stopTime);
    Simulator::Run ();

    if (printRedStats)
    {
        QueueDisc::Stats st = queueDiscs.Get(0)->GetStats();
        std::cout << "*** RED stats from S0 queue disc ***" << std::endl;
        std::cout << st << std::endl;

        st = queueDiscs.Get(1)->GetStats();
        std::cout << "*** RED stats from S1 queue disc ***" << std::endl;
        std::cout << st << std::endl;
    }

    Simulator::Destroy ();
    NS_LOG_INFO ("Done.");


    return 0;
}