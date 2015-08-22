#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# events.py
# Encapsulates information of the hardware events and subevents
#
##############################################################################
#
# Copyright (c) 2015 Abel Serrano <abeserra@ucm.es>
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

class Event:
  
  def __init__(self):
    self.name = ''
    self.descp = ''
    self.code = '0x'
    self.subevts = []
    self.flags = {}
    
  def addSubEvt(self,subevt):
    # Add a SubEvent object to the Event object
    self.subevts.append(subevt)
  
  def addEvtFlag(self,flag_name,flag_value):
    # Add a flag to this Event object
    self.flags[flag_name] = flag_value

class Subevent:

  def __init__(self):
    self.name = ''
    self.descp = ''
    self.flags = {}

  def addSubEvtFlag(self,flag_name,flag_value):
    # Add a flag to the Subevent object
    self.flags[flag_name] = flag_value

class PMCDescriptor:
  
  def __init__(self):
    self.name = ''
    self.pmc_type = ''
    self.pmc_number = -1
