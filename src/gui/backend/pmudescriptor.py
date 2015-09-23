# -*- coding: utf-8 -*-

#
# pmudescriptor.py
# Encapsulates machine info.
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

from pmc_connect import *

class Field:
  
  def __init__(self):
    self.name = ''
    self.nbits = 0
    self.default = 0

class MachineInfo:
  def __init__(self):
    self.vendor = ''
    self.nr_cores = 0
    self.nr_fields = 0
    self.nr_virt_counters = 0
    self.fields = None
    
  def addField(self,flag):
    # add a flag to this PMUDescriptor
    if self.fields is None:
      self.fields = [flag]
    else:
      self.fields.append(flag)
    
    self.nr_fields += 1
  
class PMUDescriptor:

  def __init__(self):
    self.model = ''
    self.nr_gp_pmcs = 0
    self.nr_ff_pmcs = 0
    self.pmc_bitwidth = 0

def createInfoInstances(pmc_connect,model=None):
    def extractVendor(model):
        if (model.find('amd') != -1):
          return 'amd'
        elif (model.find('arm') != -1):
          return 'arm'
        elif (model.find('intel') != -1):
          return 'intel'
        else:
          return 'unknown' 

    import re;

    machine_info = MachineInfo()
    pmuinfo_list = []
    virtual_counters_desc = []
    
    pmcinfo = pmc_connect.ReadFile('/proc/pmc/info')
    
    if model:
        nr_core_types = 1
    else:
        match = re.search(r'nr_core_types=(\d+)',pmcinfo)
        nr_core_types = int(match.group(1))
    
    # Init pmuinfo_list
    for i in range(nr_core_types):
        pmuinfo_list.append(PMUDescriptor())

    # Getting data from /proc/pmc/info:
    nr_gp_pmcs_matches = re.findall(r'nr_gp_pmcs=(\d+)',pmcinfo)
    nr_ff_pmcs_matches = re.findall(r'nr_ff_pmcs=(\d+)',pmcinfo)
    pmc_bitwidth_matches = re.findall(r'pmc_bitwidth=(\d+)',pmcinfo)
    pmu_model_matches = re.findall(r'pmu_model=([\w\.\-]+)',pmcinfo)
    
    match = re.search(r'nr_virtual_counters=(\d+)',pmcinfo)
    nr_virtual_counters = int(match.group(1))

    for nr_vcounter in range(nr_virtual_counters):
        match = re.search(r'virt' + str(nr_vcounter)  + '=(\w+)',pmcinfo)
        virtual_counters_desc.append(match.group(1))

    
    # Calculating nr_cores from /proc/cpuinfo
    nr_cores = 0
    pat_processor = r'^processor'
    cpuinfo = pmc_connect.ReadFile('/proc/cpuinfo').split('\n')
    for line in cpuinfo:
      match = re.search(pat_processor,line)
      if match:
        nr_cores += 1
    
    ##### IMPORTANT, DELETE THE NEXT LINE #####
    nr_core_types = 1

    # Assigning data to each pmuinfo
    for i in range(nr_core_types):
        pmuinfo_list[i].nr_gp_pmcs = int(nr_gp_pmcs_matches[i])
        pmuinfo_list[i].nr_ff_pmcs = int(nr_ff_pmcs_matches[i])
        pmuinfo_list[i].pmc_bitwidth = int(pmc_bitwidth_matches[i])
        if not model:
          pmuinfo_list[i].model = pmu_model_matches[i]
        else:
          pmuinfo_list[i].model = model
  
    # Assigning data to machine_info
    machine_info.vendor = extractVendor(pmuinfo_list[0].model)
    machine_info.nr_cores = nr_cores;

    return machine_info, pmuinfo_list, virtual_counters_desc
