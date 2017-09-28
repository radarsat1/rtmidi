//*****************************************//
//  testrtmidi.cpp
//  by Stephen Sinclair, 2017
//
//  Unit test various configurations of RtMidi with random messages;
//  checks whether sent messages are received and whether the timing
//  is correct.
//
//*****************************************//

#include <iostream>
#include <cstdlib>
#include <string>
#include <memory>
#include <signal.h>
#include <cmath>
#include "RtMidi.h"

// Platform-dependent sleep routines.
#if defined(__WINDOWS_MM__)
  #include <windows.h>
  #define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
  #include <unistd.h>
  #define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

// Platform-dependent get-time routines.
#ifdef HAVE_CHRONO
#include <chrono>
struct Timer {
  using tp = std::chrono::high_resolution_clock::time_point; tp t0;
  Timer() { t0 = std::chrono::high_resolution_clock::now(); }
  double get() { tp t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>
      (t1 - t0).count() * 1e-6; }
};
#else
#if defined(__WINDOWS_MM__)
  #include <windows.h>
  struct Timer {
    LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds, Frequency }
    Timer() { QueryPerformanceFrequency(&t.Frequency);
              QueryPerformanceCounter(&t.StartingTime); }
    double get() { QueryPerformanceCounter(&EndingTime);
      ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
      ElapsedMicroseconds.QuadPart *= 1000000;
      ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
      return ElapsedMicroseconds.QuadPart * 1e6;
    }};
#else // Unix variants
  #include <unistd.h>
  #include <sys/time.h>
  struct Timer {
    struct timeval t0;
    Timer() { gettimeofday(&t0, NULL); }
    double get() { struct timeval t1; gettimeofday(&t1, NULL);
      return t1.tv_sec - t0.tv_sec + (t1.tv_usec + (1e6-t0.tv_usec))*1e-6 - 1;
    }};
#endif
#endif

bool done=false;
static void finish( int /*ignore*/ ){ done = true; }
bool wait_connect = false;

// A single midi message.
typedef std::vector<unsigned char> midiMsg;

// List of delta time, message pairs.
typedef std::vector< std::pair<double, midiMsg> > midiMsgList;

void usage( void ) {
  // Error function in case of incorrect command-line
  // argument specifications.
  std::cout << "\nusage: qmidiin <port>\n";
  std::cout << "    where port = the device to use (default = 0).\n\n";
  exit( 0 );
}

double recvBlocking(std::shared_ptr<RtMidiIn> &in, midiMsg& msg)
{
  double stamp = in->getMessage( &msg );
  while ( !done && msg.empty() ) {
    SLEEP(1);
    stamp = in->getMessage( &msg );
  }

  std::cout << "[" << stamp << "] ";
  for (const auto& c : msg)
  {
    int i = c;
    std::cout << i << " ";
  }
  std::cout << std::endl;

  return stamp;
}

// Send and receive a list of messages.
// The timestamp is ABSOLUTE, not delta.
midiMsgList sendRecvList(std::shared_ptr<RtMidiIn> &in,
                         std::shared_ptr<RtMidiOut> &out,
                         const midiMsgList& list)
{
  if (list.size()==0) return {};
  midiMsgList::size_type pos_out = 0;
  midiMsgList received;
  Timer timer;
  double t0out = list[0].first;
  double t0in = 0.0;
  double timerecv = 0;
  midiMsg msg;
  while (!done && received.size() < list.size())
  {
    SLEEP(1);

    double stamp = in->getMessage(&msg);
    if (msg.size() > 0) {
      if (received.size()==0)
        t0in = stamp;

      // Ignore the delta difference of the very first message; the
      // timestamps are offset to make all absolute times should match
      // for subsequent messages.
      received.push_back({stamp + timerecv - t0in + t0out, msg});
      timerecv = timer.get();
    }

    if (pos_out < list.size()) {
      double t1 = list[pos_out].first;
      double t = timer.get();
      if (t >= (t1 - t0out)) {
        std::cout << t << std::endl;
        out->sendMessage(&list[pos_out++].second);
      }
    }
  }

  return received;
}

// Compare two lists of MIDI messages with ABSOLUTE timestamps.
// Return the RMS difference in seconds between message deltas.
// Return -1 if there is a contents mismatch.
double compareSentReceivedList(midiMsgList& sent,
                               midiMsgList& received,
                               bool verbose=true)
{
  double sum_diff_sec = 0.0;

  if (verbose)
    std::cout << "== Received: " << std::endl;
  for (midiMsgList::size_type i=0; i < received.size(); i++)
  {
    double cur_in = sent[i].first;
    double cur_out = received[i].first;
    double last_in = i > 0 ? sent[i-1].first : 0.0;
    double last_out = i > 0 ? received[i-1].first : 0.0;
    double delta_in = (cur_in - last_in);
    double delta_out = (cur_out - last_out);
    double diff_delta = std::fabs(delta_in - delta_out);
    if (verbose)
      std::cout << "[" << cur_in << ", " << cur_out << "] ";
    sum_diff_sec += diff_delta*diff_delta;

    if (verbose)
    {
      for (const auto& c : received[i].second)
      {
        int i = c;
        std::cout << i << " ";
      }
      std::cout << std::endl;
    }

    if (received[i].second.size() != sent[i].second.size())
      return -1;

    for (midiMsg::size_type c=0; c < received[i].second.size(); c++)
      if (received[i].second.at(c) != sent[i].second.at(c))
        return -1;
  }

  double rms = std::sqrt(sum_diff_sec / received.size());
  if (verbose)
    std::cout << "Average error of delta times: " << (rms*1e3) << " ms" << std::endl;
  return rms;
}

int setupInOut(std::shared_ptr<RtMidiIn>& in,
               std::shared_ptr<RtMidiOut>& out)
{
  in = std::make_shared<RtMidiIn>();
  out = std::make_shared<RtMidiOut>();

  int port_in=0, port_out=0;

  // Open a virtual input port
  in->openVirtualPort("TestRtMidi");

  // Open an output port connected to it (search by name)
  int n_out = out->getPortCount();
  if (n_out==0) {
    std::cout << "No output ports available." << std::endl;
    return 1;
  }
  else if (n_out==1) {
    port_in = 0;
    out->openPort(port_in);
  }
  else {
    std::cout << n_out << " output ports found." << std::endl;
    int i=0;
    for (; i<n_out; i++)
    {
      std::string name = out->getPortName(i);
      if (name.find("TestRtMidi") != std::string::npos)
      {
        port_out = i;
        break;
      }
    }
    std::cout << "Opening output port \"" << out->getPortName(port_out)
              << "\"" << std::endl;
    out->openPort(port_out);
  }

  // Let user connect them if requested
  if (wait_connect)
  {
    std::cout << "Hit enter when you have connected MIDI ports \""
              << in->getPortName() << "\" and \"" << out->getPortName()
              << "\"." << std::endl;
    std::string tmp;
    std::getline( std::cin, tmp );
  }

  // Default "ignore" configuration for testing:
  // Don't ignore sysex, timing, or active sensing messages.
  in->ignoreTypes( false, false, false );

  // Try for a while to verify that they are connected.
  // Use a simple note-on as a signal.
  midiMsg msg({144, 100, 90});
  out->sendMessage(&msg);

  // Receive, and check message contents.
  if (recvBlocking(in, msg) == 0.0
      && msg.size()==3
      && msg[0]==144 && msg[1]==100 && msg[2]==90)
  {
    std::cout << "Connected." << std::endl;
    return 0;
  }
  else
    return 1;
}

int test_timer();
int test_notes();

int main(int argc, char *argv[])
{
  // Install an interrupt handler function.
  done = false;
  (void) signal(SIGINT, finish);

  if (argc > 1 && (std::string(argv[1])=="--wait-connect"
                   || std::string(argv[1])=="-w"))
    wait_connect = true;

#define STEST(x,y) if(test_##x()){std::cout<<"Error in test_"<<y<<std::endl;return 1;}
#define TEST(x) STEST(x, #x)

  TEST(timer);
  TEST(notes);

  return 0;
}

int test_timer()
{
  // Sanity test for the timer that will be used to verify the other tests
  std::cout << "Testing timer." << std::endl;

  double perc_diff = 0;
  { Timer t; SLEEP(1);    perc_diff += t.get() * 100 / 1*1e-3; }
  { Timer t; SLEEP(10);   perc_diff += t.get() * 100 / 10*1e-3; }
  { Timer t; SLEEP(100);  perc_diff += t.get() * 100 / 100*1e-3; }
  { Timer t; SLEEP(1000); perc_diff += t.get() * 100 / 1000*1e-3; }
  perc_diff /= 4;
  std::cout << "Timer difference: " << perc_diff << "%" << std::endl;
  // Error if greater than 5% difference
  return perc_diff > 5;
}

int test_notes()
{
  // Test sending some basic notes delays between them.
  std::cout << "Testing notes." << std::endl;

  std::shared_ptr<RtMidiIn> in;
  std::shared_ptr<RtMidiOut> out;
  if (setupInOut(in, out)) return 1;

  midiMsgList msgList(
    {{0.1, {144, 100, 90}},
     {0.3, {144, 120, 70}},
     {1.5, {144, 110, 30}},
     {2.0, {144, 100, 40}},
    });
  midiMsgList result = sendRecvList(in, out, msgList);

  if (compareSentReceivedList(msgList, result) < 0) {
    std::cout << "Mismatch error in transmitted message list." << std::endl;
    return 1;
  }

  return 0;
}
