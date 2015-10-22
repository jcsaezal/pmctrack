# -*- coding: utf-8 -*-

#
# pmc_connect.py
# Component for connecting to remote machines using SSH or local machine.
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
		if info_machine.is_remote:
			self.ssh_array = info_machine.GetSSHCommand().split()

	def CheckFileExists(self, file):
            if file != "" and file != "\n" and file.find("#") == -1:
		if self.info_machine.is_remote:
                    command = self.ssh_array[:]
		    command.append("ls " + file)
		    pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		    stdout, stderr = pipe.communicate()
		    ok = (stderr == "")
		else:
		    ok = os.path.exists(file)
            else:
                ok = False
	    return ok

	def CheckPkgInstalled(self, pkg, remote):
		command = None
                if remote: command = self.ssh_array[:]
		else: command = []
		command.append(pkg)
		try:
			pipe = Popen(command, stdout=PIPE, stderr=PIPE)
			stdout, stderr = pipe.communicate()
			installed = (stderr == "")
		except:
			installed = False
		return installed
			
	def CheckConnectivity(self):
                command = self.ssh_array[:]
		command.append("whoami")
		pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		stdout, stderr = pipe.communicate()
		msg_error = ""
		if (stdout.find(self.info_machine.remote_user) != 0):
			if stderr.find("publickey,password") >= 0:
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
		if self.info_machine.is_remote:
                    command = self.ssh_array[:]
		    command.append("cat " + file)
		    pipe = Popen(command, stdout=PIPE, stderr=PIPE)
		    result = pipe.stdout.read()
		else:
		    descrip = open(file, 'rU')
		    result = descrip.read()
		    descrip.close()
		return result
