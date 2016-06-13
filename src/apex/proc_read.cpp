#if defined(APEX_HAVE_PROC)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include "proc_read.h"
#include "apex_api.hpp"
#include "apex.hpp"
#include <sstream>
#include <string>
#ifdef __MIC__
#include "boost/regex.hpp"
#define REGEX_NAMESPACE boost
#else
#include <regex>
#define REGEX_NAMESPACE std
#endif
#include <set>
#include "utils.hpp"
#include <chrono>

#define COMMAND_LEN 20
#define DATA_SIZE 512

#ifdef APEX_HAVE_TAU
#define PROFILING_ON
#define TAU_DOT_H_LESS_HEADERS
#include <TAU.h>
#endif

#ifdef APEX_HAVE_LM_SENSORS
#include "sensor_data.hpp"
#endif

#ifdef APEX_HAVE_MSR
#include <msr/msr_core.h>
#include <msr/msr_rapl.h>
#endif

using namespace std;

namespace apex {

void get_popen_data(char *cmnd) {
    FILE *pf;
    string command;
    char data[DATA_SIZE];
 
    // Execute a process listing
    command = "cat /proc/"; 
    command = command + cmnd; 
 
    // Setup our pipe for reading and execute our command.
    pf = popen(command.c_str(),"r"); 
 
    if(!pf){
      cerr << "Could not open pipe for output." << endl;
      return;
    }
 
    // Grab data from process execution
    while ( fgets( data, DATA_SIZE, pf)) {
      cout << "-> " << data << endl; 
      fflush(pf);
    }
 
    if (pclose(pf) != 0) {
        cerr << "Error: Failed to close command stream" << endl;
    }
 
    return;
}

ProcData* parse_proc_stat(void) {
  if (!apex_options::use_proc_stat()) return NULL;

  /*  Reading proc/stat as a file  */
  FILE * pFile;
  char line[128];
  char dummy[32];
  pFile = fopen ("/proc/stat","r");
  ProcData* procData = new ProcData();
  if (pFile == NULL) perror ("Error opening file");
  else {
    CPUStat* cpu_stat;
    while ( fgets( line, 128, pFile)) {
      if ( strncmp (line, "cpu", 3) == 0 ) { 
        cpu_stat = new CPUStat();
        /*  Note, this will only work on linux 2.6.24 through 3.5  */
        sscanf(line, "%s %lld %lld %lld %lld %lld %lld %lld %lld %lld\n", 
            cpu_stat->name, &cpu_stat->user, &cpu_stat->nice, 
            &cpu_stat->system, &cpu_stat->idle, 
            &cpu_stat->iowait, &cpu_stat->irq, &cpu_stat->softirq, 
            &cpu_stat->steal, &cpu_stat->guest);
        procData->cpus.push_back(cpu_stat);
      }
      else if ( strncmp (line, "ctxt", 4) == 0 ) { 
        sscanf(line, "%s %lld\n", dummy, &procData->ctxt);
      } else if ( strncmp (line, "btime", 5) == 0 ) { 
        sscanf(line, "%s %lld\n", dummy, &procData->btime);
      } else if ( strncmp (line, "processes", 9) == 0 ) { 
        sscanf(line, "%s %ld\n", dummy, &procData->processes);
      } else if ( strncmp (line, "procs_running", 13) == 0 ) { 
        sscanf(line, "%s %ld\n", dummy, &procData->procs_running);
      } else if ( strncmp (line, "procs_blocked", 13) == 0 ) { 
        sscanf(line, "%s %ld\n", dummy, &procData->procs_blocked);
      //} else if ( strncmp (line, "softirq", 5) == 0 ) { 
        // softirq 10953997190 0 1380880059 1495447920 1585783785 15525789 0 12 661586214 0 1519806115
        //sscanf(line, "%s %d\n", dummy, &procData->btime);
      }
      // don't waste time parsing anything but the mean
      break;
    }
  }
  fclose (pFile);
#if defined(APEX_HAVE_CRAY_POWER)
  procData->power = read_power();
  procData->power_cap = read_power_cap();
  procData->energy = read_energy();
  procData->freshness = read_freshness();
  procData->generation = read_generation();
#endif
  return procData;
}

ProcData::~ProcData() {
  while (!cpus.empty()) {
    delete cpus.back();
    cpus.pop_back();
  }
}

ProcData* ProcData::diff(ProcData const& rhs) {
  ProcData* d = new ProcData();
  unsigned int i;
  CPUStat* cpu_stat;
  for (i = 0 ; i < cpus.size() ; i++) {
    cpu_stat = new CPUStat();
    strcpy(cpu_stat->name, cpus[i]->name);
    cpu_stat->user = cpus[i]->user - rhs.cpus[i]->user; 
    cpu_stat->nice = cpus[i]->nice - rhs.cpus[i]->nice;
    cpu_stat->system = cpus[i]->system - rhs.cpus[i]->system;
    cpu_stat->idle = cpus[i]->idle - rhs.cpus[i]->idle;
    cpu_stat->iowait = cpus[i]->iowait - rhs.cpus[i]->iowait;
    cpu_stat->irq = cpus[i]->irq - rhs.cpus[i]->irq;
    cpu_stat->softirq = cpus[i]->softirq - rhs.cpus[i]->softirq;
    cpu_stat->steal = cpus[i]->steal - rhs.cpus[i]->steal;
    cpu_stat->guest = cpus[i]->guest - rhs.cpus[i]->guest;
    d->cpus.push_back(cpu_stat);
  }
  d->ctxt = ctxt - rhs.ctxt;
  d->processes = processes - rhs.processes;
  d->procs_running = procs_running - rhs.procs_running;
  d->procs_blocked = procs_blocked - rhs.procs_blocked;
#if defined(APEX_HAVE_CRAY_POWER)
  d->power = power;
  d->power_cap = power_cap;
  d->energy = energy - rhs.energy;
  d->freshness = freshness;
  d->generation = generation;
#endif
  return d;
}

void ProcData::dump(ostream &out) {
  out << "name\tuser\tnice\tsys\tidle\tiowait\tirq\tsoftirq\tsteal\tguest" << endl;
  CPUs::iterator iter;
  long long total = 0L;
  double idle_ratio = 0.0;
  double user_ratio = 0.0;
  double system_ratio = 0.0;
  for (iter = cpus.begin(); iter != cpus.end(); ++iter) {
    CPUStat* cpu_stat=*iter;
    out << cpu_stat->name << "\t" 
        << cpu_stat->user << "\t" 
        << cpu_stat->nice << "\t" 
        << cpu_stat->system << "\t" 
        << cpu_stat->idle << "\t" 
        << cpu_stat->iowait << "\t" 
        << cpu_stat->irq << "\t" 
        << cpu_stat->softirq << "\t" 
        << cpu_stat->steal << "\t" 
        << cpu_stat->guest << endl;
    if (strcmp(cpu_stat->name, "cpu") == 0) {
      total = cpu_stat->user + cpu_stat->nice + cpu_stat->system + cpu_stat->idle + cpu_stat->iowait + cpu_stat->irq + cpu_stat->softirq + cpu_stat->steal + cpu_stat->guest;
      user_ratio = (double)cpu_stat->user / (double)total;
      system_ratio = (double)cpu_stat->system / (double)total;
      idle_ratio = (double)cpu_stat->idle / (double)total;
    }
  }
  out << "ctxt " << ctxt << endl;
  out << "processes " << processes << endl;
  out << "procs_running " << procs_running << endl;
  out << "procs_blocked " << procs_blocked << endl;
  out << "user ratio " << user_ratio << endl;
  out << "system ratio " << system_ratio << endl;
  out << "idle ratio " << idle_ratio << endl;
  //out << "softirq %d\n", btime);
}

void ProcData::dump_mean(ostream &out) {
  CPUs::iterator iter;
  iter = cpus.begin();
    CPUStat* cpu_stat=*iter;
    out << cpu_stat->name << "\t" 
        << cpu_stat->user << "\t" 
        << cpu_stat->nice << "\t" 
        << cpu_stat->system << "\t" 
        << cpu_stat->idle << "\t" 
        << cpu_stat->iowait << "\t" 
        << cpu_stat->irq << "\t" 
        << cpu_stat->softirq << "\t" 
        << cpu_stat->steal << "\t" 
        << cpu_stat->guest << endl;
}

void ProcData::dump_header(ostream &out) {
  out << "name\tuser\tnice\tsys\tidle\tiowait\tirq\tsoftirq\tsteal\tguest" << endl;
}

double ProcData::get_cpu_user() {

  CPUs::iterator iter;
  long long total = 0L;
  double user_ratio = 0.0;
  for (iter = cpus.begin(); iter != cpus.end(); ++iter) {
    CPUStat* cpu_stat=*iter;
      if (strcmp(cpu_stat->name, "cpu") == 0) {
        total = cpu_stat->user + cpu_stat->nice + cpu_stat->system + cpu_stat->idle + cpu_stat->iowait + cpu_stat->irq + cpu_stat->softirq + cpu_stat->steal + cpu_stat->guest;
        user_ratio = (double)cpu_stat->user / (double)total;
      break;
      }
  }

  return user_ratio;

}

void ProcData::write_user_ratios(ostream &out, double *ratios, int num) {

  for (int i=0; i<num; i++) {
    out << ratios[i] << " ";
  }
  out << endl;

}

ProcStatistics::ProcStatistics(int size) {

  this->min = (long long*) malloc (size*sizeof(long long));
  this->max = (long long*) malloc (size*sizeof(long long));
  this->mean = (long long*) malloc (size*sizeof(long long));
  this->size = size;

}

//ProcStatistics::~ProcStatistics() {
//  free (this->min);
//  free (this->max);
//  free (this->mean);
//}

int ProcStatistics::getSize() {
  return this->size;
}

void ProcData::sample_values(void) {
  double total;
  CPUs::iterator iter = cpus.begin();
  CPUStat* cpu_stat=*iter;
  total = (double)(cpu_stat->user + cpu_stat->nice + cpu_stat->system + cpu_stat->idle + cpu_stat->iowait + cpu_stat->irq + cpu_stat->softirq + cpu_stat->steal + cpu_stat->guest);
  total = total * 0.01; // so we have a percentage in the final values
  sample_value("CPU User %",     ((double)(cpu_stat->user))    / total);
  sample_value("CPU Nice %",     ((double)(cpu_stat->nice))    / total);
  sample_value("CPU System %",   ((double)(cpu_stat->system))  / total);
  sample_value("CPU Idle %",     ((double)(cpu_stat->idle))    / total);
  sample_value("CPU I/O Wait %", ((double)(cpu_stat->iowait))  / total);
  sample_value("CPU IRQ %",      ((double)(cpu_stat->irq))     / total);
  sample_value("CPU soft IRQ %", ((double)(cpu_stat->softirq)) / total);
  sample_value("CPU Steal %",    ((double)(cpu_stat->steal))   / total);
  sample_value("CPU Guest %",    ((double)(cpu_stat->guest))   / total);
#if defined(APEX_HAVE_CRAY_POWER)
  sample_value("Power", power);
  sample_value("Power Cap", power_cap);
  sample_value("Energy", energy);
  sample_value("Freshness", freshness);
  sample_value("Generation", generation);
#endif
}

bool parse_proc_cpuinfo() {
  if (!apex_options::use_proc_cpuinfo()) return false;

  FILE *f = fopen("/proc/cpuinfo", "r");
  if (f) {
    char line[4096] = {0};
    int cpuid = 0;
    while ( fgets( line, 4096, f)) {
        string tmp(line);
        const REGEX_NAMESPACE::regex separator(":");
        REGEX_NAMESPACE::sregex_token_iterator token(tmp.begin(), tmp.end(), separator, -1);
        REGEX_NAMESPACE::sregex_token_iterator end;
        string name = *token++;
        if (token != end) {
          string value = *token;
          name = trim(name);
          char* pEnd;
          double d1 = strtod (value.c_str(), &pEnd);
          if (strcmp(name.c_str(), "processor") == 0) { cpuid = (int)d1; }
          stringstream cname;
          cname << "cpuinfo." << cpuid << ":" << name;
          if (pEnd) { sample_value(cname.str(), d1); }
        }
    }
    fclose(f);
  } else {
    return false;
  }
  return true;
}

bool parse_proc_meminfo() {
  if (!apex_options::use_proc_meminfo()) return false;
  FILE *f = fopen("/proc/meminfo", "r");
  if (f) {
    char line[4096] = {0};
    while ( fgets( line, 4096, f)) {
        string tmp(line);
        const REGEX_NAMESPACE::regex separator(":");
        REGEX_NAMESPACE::sregex_token_iterator token(tmp.begin(), tmp.end(), separator, -1);
        REGEX_NAMESPACE::sregex_token_iterator end;
        string name = *token++;
        if (token != end) {
            string value = *token;
            char* pEnd;
            double d1 = strtod (value.c_str(), &pEnd);
            string mname("meminfo:" + name);
            if (pEnd) { sample_value(mname, d1); }
        }
    }
    fclose(f);
  } else {
    return false;
  }
  return true;
}

bool parse_proc_self_status() {
  if (!apex_options::use_proc_self_status()) return false;
  FILE *f = fopen("/proc/self/status", "r");
  const std::string prefix("Vm");
  if (f) {
    char line[4096] = {0};
    while ( fgets( line, 4096, f)) {
        string tmp(line);
        if (!tmp.compare(0,prefix.size(),prefix)) {
            const REGEX_NAMESPACE::regex separator(":");
            REGEX_NAMESPACE::sregex_token_iterator token(tmp.begin(), tmp.end(), separator, -1);
            REGEX_NAMESPACE::sregex_token_iterator end;
            string name = *token++;
            if (token != end) {
                string value = *token;
                char* pEnd;
                double d1 = strtod (value.c_str(), &pEnd);
                string mname("self_status:" + name);
                if (pEnd) { sample_value(mname, d1); }
            }
        }
    }
    fclose(f);
  } else {
    return false;
  }
  return true;
}

bool parse_proc_netdev() {
  if (!apex_options::use_proc_net_dev()) return false;
  FILE *f = fopen("/proc/net/dev", "r");
  if (f) {
    char line[4096] = {0};
    char * rc = fgets(line, 4096, f); // skip this line
    if (rc == NULL) {
        fclose(f);
        return false;
    }
    rc = fgets(line, 4096, f); // skip this line
    if (rc == NULL) {
        fclose(f);
        return false;
    }
    while (fgets(line, 4096, f)) {
        string outer_tmp(line);
        outer_tmp = trim(outer_tmp);
        const REGEX_NAMESPACE::regex separator("[|:\\s]+");
        REGEX_NAMESPACE::sregex_token_iterator token(outer_tmp.begin(), outer_tmp.end(), separator, -1);
        REGEX_NAMESPACE::sregex_token_iterator end;
        string devname = *token++; // device name
        string tmp = *token++;
        char* pEnd;
        double d1 = strtod (tmp.c_str(), &pEnd);
        string cname = devname + ".receive.bytes";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.packets";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.errs";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.drop";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.fifo";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.frame";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.compressed";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".receive.multicast";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.bytes";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.packets";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.errs";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.drop";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.fifo";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.colls";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.carrier";
        sample_value(cname, d1);

        tmp = *token++;
        d1 = strtod (tmp.c_str(), &pEnd);
        cname = devname + ".transmit.compressed";
        sample_value(cname, d1);
    }
    fclose(f);
  } else {
    return false;
  }
  return true;
}

// there will be N devices, with M sensors per device.
bool parse_sensor_data() {
#if 0
  string prefix = "/Users/khuck/src/xpress-apex/proc/power/";
  // Find out how many devices have sensors
  std::set<string> devices;
  devices.append(string("coretemp.0"));
  devices.append(string("coretemp.1"));
  devices.append(string("i5k_amb.0"));
  for (std::unordered_set<string>::const_iterator it = devices.begin(); it != devices.end(); it++) {
    // for each device, find out how many sensors there are.
  }
#endif
  return true;
}

/* This is the main function for the reader thread. */
void* proc_data_reader::read_proc(void * _ptw) {
  pthread_wrapper* ptw = (pthread_wrapper*)_ptw;
  static bool _initialized = false;
  if (!_initialized) {
      initialize_worker_thread_for_TAU();
      _initialized = true;
  }
#ifdef APEX_HAVE_TAU
  if (apex_options::use_tau()) {
    TAU_START("proc_data_reader::read_proc");
  }
#endif
#ifdef APEX_HAVE_LM_SENSORS
  sensor_data * mysensors = new sensor_data();
#endif
  ProcData *oldData = parse_proc_stat();
  // disabled for now - not sure that it is useful
  parse_proc_cpuinfo(); // do this once, it won't change.
  parse_proc_meminfo(); // some things change, others don't...
  parse_proc_self_status(); // some things change, others don't...
  parse_proc_netdev();
#ifdef APEX_HAVE_LM_SENSORS
  mysensors->read_sensors();
#endif
  ProcData *newData = NULL;
  ProcData *periodData = NULL;
  
  while(ptw->wait()) {
#ifdef APEX_HAVE_TAU
    if (apex_options::use_tau()) {
      TAU_START("proc_data_reader::read_proc: main loop");
    }
#endif
    if (apex_options::use_proc_stat()) {
        // take a reading
        newData = parse_proc_stat();
        periodData = newData->diff(*oldData);
        // save the values
        periodData->sample_values();
        // free the memory
        delete(oldData);
        delete(periodData);
        oldData = newData;
    }
    parse_proc_meminfo(); // some things change, others don't...
    parse_proc_self_status();
    parse_proc_netdev();

#ifdef APEX_HAVE_LM_SENSORS
    mysensors->read_sensors();
#endif

#ifdef APEX_HAVE_TAU
    if (apex_options::use_tau()) {
      TAU_STOP("proc_data_reader::read_proc: main loop");
    }
#endif
  }
#ifdef APEX_HAVE_LM_SENSORS
  delete(mysensors);
#endif

#ifdef APEX_HAVE_TAU
  if (apex_options::use_tau()) {
    TAU_STOP("proc_data_reader::read_proc");
  }
#endif
  delete(oldData);
  return nullptr;
}

#ifdef APEX_HAVE_MSR
void apex_init_msr(void) {
    int status = init_msr();
    if(status) {
        fprintf(stderr, "Unable to initialize libmsr: %d.\n", status);
        return;
    }
    struct rapl_data * r = NULL;
    uint64_t * rapl_flags = NULL;
    status = rapl_init(&r, &rapl_flags);
    if(status < 0) {
        fprintf(stderr, "Unable to initialize rapl component of libmsr: %d.\n", status);
        return;
    }
}

void apex_finalize_msr(void) {
    finalize_msr();
}

double msr_current_power_high(void) {
    static int initialized = 0;
    static uint64_t * rapl_flags = NULL;
    static struct rapl_data * r = NULL;
    static uint64_t sockets = 0;

    if(!initialized) {
        sockets = num_sockets();
        int status = rapl_storage(&r, &rapl_flags);
        if(status) {
            fprintf(stderr, "Error in rapl_storage: %d.\n", status);
            return 0.0;
        }
        initialized = 1;
    }

    poll_rapl_data();
    double watts = 0.0;
    for(int s = 0; s < sockets; ++s) {
        if(r->pkg_watts != NULL) {
            watts += r->pkg_watts[s];
        }    
    }
    return watts;
}
#endif

}

#endif // APEX_HAVE_PROC
