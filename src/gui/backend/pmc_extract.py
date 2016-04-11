# -*- coding: utf-8 -*-

#
# pmc_extract.py
# Component for getting data from pmctrack command-line tool.
#
##############################################################################
#
# Copyright (c) 2015 Jorge Casas <jorcasas@ucm.es>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA 02110-1301, USA.
#
##############################################################################

import threading
import time
import os
import signal
from subprocess import Popen, PIPE

class PMCExtract(object):
	def __init__(self, user_config):
		self.user_config = user_config

                # Dictionary data extracted from pmctrack commands. Format: data[app_name][pid/cpu][nr_experiment][nr_metric][pos_data]
		self.data = {}
                
		# Dictionary that contains characters that indicates the apps' state: 
                # waiting (W), running (R), stopped (S), killed (K) or finished (F)
		self.state = {}

                # Dictionary that contains the complete path and arguments of each application.
                self.path = {}

                # Dictionary that contains the apps' PID (only in per-thread mode)
                self.pid = {}
                
                # Are initialized structures discussed above
                for path_app in self.user_config.applications:
                    app_name = path_app.split("/")[-1]
                    self.data[app_name] = {}
                    self.state[app_name] = 'W'
                    self.path[app_name] = path_app
                    self.pid[app_name] = None

                # String that indicates the app name is being monitored actually
                self.app_running = None

                # Indicates if monitoring data is from a thread or a CPU
                if self.user_config.system_wide:
                    self.pack = "cpu"
                else:
                    self.pack = "pid"

		# If different from None, it contains an error string from the pmctrack commands
		self.error = None

                # Stores pipe to get pmctrack commands data
                self.pipe = None

		# Dictionary containing column position in which each field is located in the pmctrack command of each application
		self.pos = {}
		
		# File descriptors if user specified to save counters or metrics logs
		self.out_counters = None
		self.out_metrics = None
		
                # Thread responsible for processing pmctrack commands data keeping them in an organized self.data dictionary
		self.thread_extract = None
                self.thread_extract = threading.Thread(target = self.__extract_information)
		self.thread_extract.start()

	def StopMonitoring(self):
                if self.user_config.system_wide:
                    self.__pause_resume_syswide("pause")
                else:
		    self.__send_signal_to_application("SIGSTOP")
		self.state[self.app_running] = 'S'

	def ResumeMonitoring(self):
                if self.user_config.system_wide:
                    self.__pause_resume_syswide("resume")
                else:
		    self.__send_signal_to_application("SIGCONT")
		self.state[self.app_running] = 'R'
		
	def KillMonitoring(self):
                os.kill(self.pipe.pid, signal.SIGKILL)
		self.state[self.app_running] = 'K'

	def __send_signal_to_application(self, str_signal):
		if self.pid[self.app_running] != None and self.state[self.app_running] in ['R', 'S']:
			if not self.user_config.machine.is_remote:
				Popen(("kill -s " + str_signal + " " + self.pid[self.app_running]).split())
			else:
				comando = self.user_config.machine.GetSSHCommand().split()
				comando.append("kill -s " + str_signal + " " + self.pid[self.app_running])
				Popen(comando)

        def __pause_resume_syswide(self, arg):
			if not self.user_config.machine.is_remote:
				Popen(("echo -n syswide " + arg + " > /proc/pmc/enable").split())
			else:
				comando = self.user_config.machine.GetSSHCommand().split()
				comando.append("echo -n syswide " + arg + " > /proc/pmc/enable")
				Popen(comando)

        def __create_pipe(self, app):
            if not self.user_config.machine.is_remote:
	        self.pipe = Popen(self.__get_command(app).split(), stdout=PIPE, stderr=PIPE)
	    else:
		comando = self.user_config.machine.GetSSHCommand().split()
		comando.append("-t")
		comando.append(self.__get_command(app) + " 2>&1")
		#comando.append(self.__get_command(app) + " 2> /dev/null")
		self.pipe = Popen(comando, stdout=PIPE, stderr=PIPE)

        def __create_log_file_descriptors(self, app, line_head):
		if self.user_config.machine.is_remote:
		    machine = self.user_config.machine.remote_address
		else:
		    machine = _("local")
                
                out_header = _("Generated by PMCTrack-GUI on {0}").format(time.strftime("%c")) + "\n\n"
                out_header += _("Command launched on '{0}':").format(machine) + "\n"
                out_header += self.__get_command(app).encode('utf-8') + "\n\n"
		    
                if self.user_config.save_counters_log:
                    self.out_counters = open(self.user_config.path_outfile_logs + "/pmctrack-gui_{0}_{1}_{2}_counters.log".format(app.split()[0], machine, time.strftime("%d.%m.%y-%H.%M.%S")), "w")
                    self.out_counters.write(out_header)
		    self.out_counters.write(line_head)
			
                if self.user_config.save_metrics_log:
		    self.out_metrics = open(self.user_config.path_outfile_logs + "/pmctrack-gui_{0}_{1}_{2}_metrics.log".format(app.split()[0], machine, time.strftime("%d.%m.%y-%H.%M.%S")), "w")
                    self.out_metrics.write(out_header)
                    metrics_header = "nsample" + " " + self.pack.rjust(6) + " "
		    for num_exp in range(len(self.user_config.experiments)):
                        for metric_conf in self.user_config.experiments[num_exp].metrics:
                            self.out_metrics.write("{0} = {1} [Experiment {2}]\n".format(metric_conf.name, metric_conf.str_metric, num_exp+1))
                            metrics_header += metric_conf.name.rjust(13) + " "
                    self.out_metrics.write("\n" + metrics_header + "\n")

	def __extract_information(self):
            for app in self.data.keys():
                self.app_running = app
                self.__create_pipe(app)
                self.pos.clear()
		line_head = self.pipe.stdout.readline()
		if line_head.find("PID found") >= 0:
			line_head = self.pipe.stdout.readline()
                # This if checks if exists pmctrack command header
		if(line_head.find("nsample") >= 0 and line_head.find(self.pack) >= 0 and line_head.find("event") >= 0):
		    ind_h = 0
		    for header in line_head.split():
		        self.pos[header] = ind_h
			ind_h += 1

                    if (self.user_config.save_counters_log or self.user_config.save_metrics_log) and self.error == None:
                        self.__create_log_file_descriptors(app, line_head)

                    # Begins monitoring application
		    self.state[self.app_running] = 'R'
                    metric_eval = 0
		    for line in iter(self.pipe.stdout.readline, ''):
		    	field = line.split()
		    	pack = field[self.pos[self.pack]]
                            
                        if self.out_counters != None:
		    	    self.out_counters.write(line)
                        if self.out_metrics != None:
                            self.out_metrics.write(field[self.pos["nsample"]].rjust(7) + " ")
                            self.out_metrics.write(pack.rjust(6) + " ")

                        # If this is the first time the pack (PID or CPU) appears in this app, prepare the structure of arrays
		    	if not pack in self.data[app]:
		    		if not self.user_config.system_wide and self.pid[app] == None:
		    			self.pid[app] = pack
		    		self.data[app][pack] = []
		    		for num_exp in range(len(self.user_config.experiments)):
		    			self.data[app][pack].append([])
		    			for num_metric in range(len(self.user_config.experiments[num_exp].metrics)):
		    				self.data[app][pack][num_exp].append([])
		    				self.data[app][pack][num_exp][num_metric] = []

		    	if len(self.user_config.experiments) > 1: num_exp = int(field[self.pos["expid"]])
		    	else: num_exp = 0

                        if self.out_metrics != None:
                            for ind_exp in range(0, num_exp):
		    	        for metric in self.user_config.experiments[ind_exp].metrics:
                                    self.out_metrics.write("-".rjust(13) + " ")

		    	num_metric = 0
		    	for metric_conf in self.user_config.experiments[num_exp].metrics:
		    	    try:
		    		metric_eval = eval(metric_conf.metric)
		    	    except ZeroDivisionError:
		    		metric_eval = 0.0
                            self.data[app][pack][num_exp][num_metric].append(metric_eval)
                            if self.out_metrics != None:
                                self.out_metrics.write("{0:13.3f} ".format(metric_eval))
                            num_metric += 1

                        if self.out_metrics != None:
                            for ind_exp in range(num_exp + 1, len(self.user_config.experiments)):
		    	        for metric in self.user_config.experiments[ind_exp].metrics:
                                    self.out_metrics.write("-".rjust(13) + " ")
                            self.out_metrics.write("\n")

                    self.app_running = None
		    if self.out_counters != None:
		        self.out_counters.close()
		    if self.out_metrics != None:
		        self.out_metrics.close()

		    if self.state[app] != 'K': 
                        self.state[app] = 'F'    
                    else:
                        break
                else: # If it failed to read the header is that there has been a mistake.
		    if line_head.find("pmctrack: ") >= 0: self.error = line_head.split("pmctrack: ")[1].split('\n')[0]
		    else: self.error = line_head
                    break

	# Returns the string of an experiment (-c pmctrack command option) previously set by the user
	def __get_experiments_command(self, num_experiment):
		str_experiment = ""
		for eventHW in self.user_config.experiments[num_experiment].eventsHW:
            		# Build event
			str_experiment += ("pmc" if len(str_experiment) == 0 else ",pmc") + str(eventHW.num_counter)
			if not eventHW.fixed:
				str_experiment += "=" + eventHW.code
        			# Build flags
            			str_flags = ""
	    			for flag_name in eventHW.flags.keys():
					str_flags += "," + flag_name + str(eventHW.num_counter) + "=" + eventHW.flags[flag_name]
            			str_experiment += str_flags
                ebs_counter = self.user_config.experiments[num_experiment].ebs_counter
                ebs_value = self.user_config.experiments[num_experiment].ebs_value
                if ebs_counter >= 0:
                    str_experiment += ",ebs" + str(ebs_counter) + "=" + ebs_value
		return str_experiment

        # Returns pmctrack command of a particular app specified from user settings
	def __get_command(self, app):
		str_command = "pmctrack -L -r"
                str_command += " -T " + "{0:.2f}".format(float(self.user_config.time) / 1000)
                if self.user_config.system_wide:
                    str_command += " -S"

                if self.user_config.cpu != None:
		    str_command += " -b " + self.user_config.cpu
		
                for num_exp in range(len(self.user_config.experiments)):
		    str_experiment = self.__get_experiments_command(num_exp)
		    if str_experiment != "":
			str_command += " -c " + str_experiment

                if self.user_config.buffer_size > 0:
                    str_command += " -k " + str(self.user_config.buffer_size)
		
                if len(self.user_config.virtual_counters) > 0:
                    str_command += " -V "
                    for i in range(len(self.user_config.virtual_counters)):
                        str_command += "virt" + str(self.user_config.virtual_counters[i])
                        if i < len(self.user_config.virtual_counters) - 1:
                            str_command += ","
		if self.user_config.pid_app_running == None:
                	str_command += " " + self.path[app]
		else:
			str_command += " -p " + self.user_config.pid_app_running
		print str_command
                return str_command
