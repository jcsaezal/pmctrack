#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# parser_xml.py
# Code for XML parser.
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

import xml.parsers.expat
# Expat is a stream-oriented XML 1.0 parser library
# Expat does not validate XMLs

from events import *
from pmudescriptor import *

debug = True

def __init_parser(filename):
	
	input_file = open(filename, 'rU')
	
	parser = xml.parsers.expat.ParserCreate()
	parser.returns_unicode = 0;
	
	return parser, input_file   

def parse_events(filename):
  
  # Sharing variables for parse_events subfunctions:
  global _events
  global _current_event, _current_subevt
  global _current_tag, _f_name_aux
  
  _current_event = _current_subevt = None
  _current_tag = _f_name_aux = ''
  
  def start_tag(tag,attrs):
    global _events, _current_event, _current_subevt, _current_tag
    
    if tag == 'events':
      _events = []
    elif tag == 'event':
      _current_event = Event()
      _events.append(_current_event)
    elif tag == 'name':
      _current_tag = 'name'
    elif tag == 'descp':
      _current_tag = 'descp'
    elif tag == 'code':
      _current_tag = 'code'
    elif tag == 'subevent':
      _current_subevt = Subevent()
      _current_event.addSubEvt(_current_subevt)
    elif tag == 'subevt_name':
      _current_tag = 'subevt_name'
    elif tag == 'subevt_code':
      _current_tag = 'subevt_code'
    elif tag == 'subevt_descp':
      _current_tag = 'subevt_descp'
    elif tag == 'flag_name':
      _current_tag = 'flag_name'
    elif tag == 'flag_value':
      _current_tag = 'flag_value'
    
  def data_handler(data):
    global _events, _current_event, _current_subevt, _current_tag, _f_name_aux;
    
    if _current_tag == '':
      return
    elif _current_tag == 'name':
      _current_event.name = data
    elif _current_tag == 'descp':
      _current_event.descp = data
    elif _current_tag == 'code':
      _current_event.code = data
    elif _current_tag == 'subevt_name':
      _current_subevt.name = data
    elif _current_tag == 'subevt_code':
      _current_subevt.code = data
    elif _current_tag == 'subevt_descp':
      _current_subevt.descp = data
    elif _current_tag == 'flag_name':
      _f_name_aux = data
    elif _current_tag == 'flag_value':
      if _current_subevt:
        _current_subevt.addSubEvtFlag(_f_name_aux,data)
      elif _current_event:
        _current_event.addEvtFlag(_f_name_aux,data)        
      
  def end_tag(tag):
    global _current_event, _current_subevt, _f_name_aux, _current_tag;
    
    if tag == 'event':
      _current_event = None
    elif tag == 'subevent':
      _current_subevt = None
    elif tag == 'field':
      _f_name_aux = None
      
    _current_tag = ''
    
  parser,input_file = __init_parser(filename)
  
  parser.StartElementHandler = start_tag
  parser.EndElementHandler = end_tag
  parser.CharacterDataHandler = data_handler
	
  parser.ParseFile(input_file)
  
  input_file.close()
  
  return _events;

def parse_layout(filename,machine_info):
    
  # Sharing variables for parse_flags subfunctions:
  global _m_info
  global _current_field, _current_tag
  
  _m_info = machine_info
  _current_field = None
  _current_tag = ''
  
  def start_tag(tag,attrs):
    global _m_info, _current_field, _current_tag

    if tag == 'field':
      _current_field = Field()
      _m_info.addField(_current_field)
    elif tag == 'name':
      _current_tag = 'name'
    elif tag == 'nbits':
      _current_tag = 'nbits'
    elif tag == 'default':
      _current_tag = 'default'
    
  def data_handler(data):
    global _current_field, _current_tag
    
    if _current_tag == '':
      return
    elif _current_tag == 'name':
      _current_field.name = data
    elif _current_tag == 'nbits':
      _current_field.nbits = data
    elif _current_tag == 'default':
      _current_field.default = data
    
  def end_tag(tag):
    global _current_field, _current_tag;
    
    if tag == 'field':
      _current_field = None
      
    _current_tag = ''
    
  parser,input_file = __init_parser(filename)
  
  parser.StartElementHandler = start_tag
  parser.EndElementHandler = end_tag
  parser.CharacterDataHandler = data_handler
	
  parser.ParseFile(input_file)
  
  input_file.close()
  
  return _m_info;
  
  
def parse_pmcs(filename):

  # Sharing variables for parse_pmcs subfunctions:
  global _pmcs
  global _current_pmc
  global _current_tag
  
  # Initializing variables. This also prevents to add invalid XML tags.
  _pmcs = []
  _current_pmc = None
  _current_tag = ''
  
  def start_tag(tag,attrs):
    global _pmcs, _current_pmc, _current_tag
    
    if tag == 'pmcs':
      _pmcs = []
    elif tag == 'pmc':
      _current_pmc = PMCDescriptor()
    elif tag == 'pmc_name':
      _current_tag = 'pmc_name'
    elif tag == 'pmc_type':
      _current_tag = 'pmc_type'
    elif tag == 'pmc_number':
      _current_tag = 'pmc_number'
    
  def data_handler(data):
    global _pmcs, _current_pmc, _current_tag

    if _current_pmc == None or _current_tag == '':
      return
    elif _current_tag == 'pmc_name':
      _current_pmc.name = data.replace('_',' ')
    elif _current_tag == 'pmc_type':
      _current_pmc.pmc_type = data
    elif _current_tag == 'pmc_number':
      _current_pmc.pmc_number = int(data)

  def end_tag(tag):
    global _pmcs, _current_pmc, _current_tag
        
    if tag == 'pmc':
      index = _current_pmc.pmc_number
      
      if index > len(_pmcs)-1:
        _pmcs.insert(index,_current_pmc)
      elif (len(_pmcs[index].name) < len(_current_pmc.name)): # we keep longer names
          _pmcs[index] = _current_pmc
      
      _current_pmc = None
      
    _current_tag = ''
    
  parser,input_file = __init_parser(filename)
  
  parser.StartElementHandler = start_tag
  parser.EndElementHandler = end_tag
  parser.CharacterDataHandler = data_handler
	
  parser.ParseFile(input_file)
  
  input_file.close()
  
  return _pmcs;  
