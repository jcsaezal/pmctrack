#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# facade_xml.py
# Interface to decouple XML backend from graphic frontend.
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
#
#  2015-04-20 Modified by Jorge Casas to provide support to remote machine
#             monitoring (SSH mode).
#

import sys
import os

import parser_xml
from pmudescriptor import *
from backend.pmc_connect import *

supported_vendors = ['amd','intel','arm','template']
xml_dir = 'backend/xml_definitions'
xml_path =os.path.join(os.path.abspath(os.path.dirname(sys.argv[0]) ), xml_dir)

def get_layout_xml_path(vendor):
  path_flags = os.path.join(xml_path, (vendor + '_layout.xml') )
  if not os.path.isfile(path_flags):
    sys.stderr.write("Error: not layout xml definition file found for '" + vendor + "'\n")
    sys.stderr.write("\t'" + path_flags + "' not found.\n")
    sys.exit(1)

  return path_flags

def get_events_xml_path(model):
  path_events = os.path.join(xml_path,(model + '.xml') )
  if not os.path.isfile(path_events):
    sys.stderr.write("Error: not events xml definition file found for '" + model + "'\n")
    sys.stderr.write("\t'" + path_events + "' not found.\n")
    sys.exit(1)
    
  return path_events
  
class FacadeXML:
  
  def __init__(self, pmc_connect, model=None):
        
    # Creating PMUDescriptors and MachineInfo instances
    self.machine_info,self.pmudescps,self.vcounters_desc = createInfoInstances(pmc_connect,model)
    
    if self.machine_info.vendor not in supported_vendors:
      sys.stderr.write("Error: unsupported vendor '" + self.pmudescps[0].vendor + "'\n")
      sys.exit(1)
    
    # Loading machine general info from xml
    self.machine_info = parser_xml.parse_layout(get_layout_xml_path(self.machine_info.vendor),self.machine_info)
    
    # Postpone parse of available events for later
    self.available_events = None
    
    # Postpone parse of fixed pmcs for later
    self.fixed_pmcs = None
  
  ### public functions served by the facade starts here ###
  
  def getNrCounters(self,model_nr=0):
    return self.pmudescps[model_nr].nr_ff_pmcs, self.pmudescps[model_nr].nr_gp_pmcs, len(self.vcounters_desc)
      
  #return names of pmus available in a list
  def getPMUList(self):
    ret_list = []
    for i in self.pmudescps:
      ret_list.append(i.model)
    return (ret_list)
  
  #return vendor, fields of the layout and nr of cores in this machine
  def getMachineInfo(self):
    return self.machine_info.vendor, self.machine_info.fields, self.machine_info.nr_cores
  
  def getAvailableEvents(self,model_nr=0):
    path = get_events_xml_path(self.pmudescps[model_nr].model)
    self.available_events = parser_xml.parse_events(path)
    self.available_events.sort(key=lambda x: str.lower(x.name) ) # sort elements by name
    
    return self.available_events
  
  def getFixedPMCs(self,model_nr=0):
    path = get_events_xml_path(self.pmudescps[model_nr].model)
    self.fixed_pmcs = parser_xml.parse_pmcs(path)
    return self.fixed_pmcs

  def getVirtCountersDesc(self):
    return self.vcounters_desc
