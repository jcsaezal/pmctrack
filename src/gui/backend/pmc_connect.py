# -*- coding: utf-8 -*-

#
# pmc_connect.py
# Component for connecting to remote machines using SSH, ADB-server or local
# machine.
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

from subprocess import Popen, PIPE
import os.path

class PMCConnect(object):
	def __init__(self, info_machine):
		self.info_machine = info_machine
		self.conn_array = None
		if info_machine.type_machine == "ssh":
			self.conn_array = info_machine.GetSSHCommand().split()
		if info_machine.type_machine == "adb":
			self.conn_array = info_machine.GetADBCommand().split()

	def CheckFileExists(self, file):
            if file != "" and file != "\n" and file.find("#") == -1:
		if self.info_machine.type_machine == "local":
		    ok = os.path.exists(file)
		else:
                    command = self.conn_array[:]
		    command.append("ls " + file)
		    pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		    stdout, stderr = pipe.communicate()
		    ok = (stdout.find(file) >= 0 and stdout.find("No such") < 0)
            else:
                ok = False
	    return ok

	def CheckPkgInstalled(self, pkg, check_in_remote):
		command = None
		if check_in_remote:
                	command = self.conn_array[:]
			command.append("which " + pkg)
		else:
			command = ["which", pkg]
		pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		stdout, stderr = pipe.communicate()
		return (stdout.find(pkg) >= 0)
			
	def CheckConnectivity(self):
                command = self.conn_array[:]
		command.append("echo hello")
		pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		stdout, stderr = pipe.communicate()
		msg_error = ""
		if (stdout.find("hello") < 0):
			if stderr.find("Cannot start server") >= 0:
				msg_error = _("No connection with ADB server.")
			elif stderr.find("publickey,password") >= 0:
				msg_error = _("The SSH key provided is invalid.")
			elif stderr.find("ermis") >= 0:
				msg_error = _("The username or password are incorrect.")
			elif stderr.find("timed out") >= 0:
				msg_error = _("Connection timed out.")
			elif stderr.find("service not known") >= 0:
				msg_error = _("Remote machine not found.")
			else:
                                msg_error = stderr
		return msg_error

	def ReadFile(self, file):
		if self.info_machine.type_machine == "local":
		    descrip = open(file, 'rU')
		    result = descrip.read()
		    descrip.close()
		else:
                    command = self.conn_array[:]
		    command.append("cat " + file)
		    pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		    result = pipe.stdout.read()
		return result
