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
#include "RtMidi.h"

// Platform-dependent sleep routines.
#if defined(__WINDOWS_MM__)
  #include <windows.h>
  #define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
  #include <unistd.h>
  #define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

bool done=false;
static void finish( int /*ignore*/ ){ done = true; }
bool wait_connect = false;

void usage( void ) {
  // Error function in case of incorrect command-line
  // argument specifications.
  std::cout << "\nusage: qmidiin <port>\n";
  std::cout << "    where port = the device to use (default = 0).\n\n";
  exit( 0 );
}

int setupInOut(std::shared_ptr<RtMidiIn>& in,
               std::shared_ptr<RtMidiOut>& out)
{
  in = std::make_shared<RtMidiIn>();
  out = std::make_shared<RtMidiOut>();

  int port_in=0, port_out=0;

  // int n_in = in->getPortCount();
  // if (n_in==0) {
  //   std::cout << "No input ports available." << std::endl;
  //   return 1;
  // }
  // else if (n_in==1) {
  //   std::cout << "Opening input port \"" << in->getPortName(port_in) << "\"" << std::endl;
  //   in->openPort(port_in);
  // }
  // else std::cout << n_in << " input ports found." << std::endl;

  in->openVirtualPort();

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
      if (name.find("RtMidi Input") != std::string::npos)
      {
        port_out = i;
        break;
      }
    }
    std::cout << "Opening output port \"" << out->getPortName(port_out) << "\"" << std::endl;
    out->openPort(port_out);
  }

  // Connect them
  if (wait_connect)
  {
    std::cout << "Hit enter when you have connected MIDI ports \""
              << in->getPortName() << "\" and \"" << out->getPortName()
              << "\"." << std::endl;
    std::string tmp;
    std::getline( std::cin, tmp );
  }

  return 0;
}

int test_notes();

int main(int argc, char *argv[])
{
  // Install an interrupt handler function.
  done = false;
  (void) signal(SIGINT, finish);

  if (argc > 1 && (std::string(argv[1])=="--wait-connect"
                   || std::string(argv[1])=="-w"))
    wait_connect = true;

  test_notes();

  return 0;
}

int test_notes()
{
  std::shared_ptr<RtMidiIn> in;
  std::shared_ptr<RtMidiOut> out;
  if (setupInOut(in, out)) return 1;

  // Don't ignore sysex, timing, or active sensing messages.
  in->ignoreTypes( false, false, false );

  SLEEP(100);

  std::vector<unsigned char> msg({144, 100, 90});
  out->sendMessage(&msg);

  SLEEP(100);

  double stamp = in->getMessage( &msg );
  while ( !(done || msg.empty()) ) {
    std::cout << "[" << stamp << "] ";
    for (const auto& c : msg)
    {
      int i = c;
      std::cout << i << " ";
    }
    std::cout << std::endl;
    stamp = in->getMessage( &msg );
  }

  return 0;
}
