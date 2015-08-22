#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# pmc_frame_counters.py
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

import wx
from backend.facade_xml import *
from backend.user_config import *
from frames.pmc_frame_machine import *
from frames.pmc_generate_exp import *
from frames.pmc_frame_final_conf import *
from frames.pmc_dialog_assign_event import *

class PMCFrameCounters(wx.Frame):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Frame.__init__(self, *args, **kwds)

	self.version = kwargs.get("version")
	self.user_config = kwargs.get("user_config")
	self.facade_xml = kwargs.get("facade_xml")
	(self.num_fc, self.num_gc, self.num_vc) = self.facade_xml.getNrCounters()
	self.button_v_counters = wx.Button(self, -1, _("Show virtual counters"))
	self.button_add_exp = wx.Button(self, -1, _("Add experiment"))
        self.button_rem_exp = wx.Button(self, -1, _("Remove experiment"))
	self.sizer_v_counters_staticbox = wx.StaticBox(self, -1, _("Virtual counters available"))
        self.sizer_v_counters = wx.StaticBoxSizer(self.sizer_v_counters_staticbox, wx.VERTICAL)
	self.v_counter_checkboxs = [] # List of graphics controls CheckBox of virtual counters available.
	self.show_v_counters = False # Indicates if shows list of virtual counters.
        self.tab_exps = wx.Notebook(self, -1, style=0)
	self.exps = [] # List of experiments added (objects of PMCGenerateExp class).
        self.button_prev = wx.Button(self, -1, "< " + _("Back"))
        self.button_next = wx.Button(self, -1, _("Next") + " >")
	self.prev_frame = None
	self.next_frame = None
        self.separator = wx.BoxSizer(wx.VERTICAL)
	
	if self.num_vc > 0:
            for vcounter in range(self.num_vc):    
		self.v_counter_checkboxs.append(wx.CheckBox(self, -1, "virt" + str(vcounter)))
        else:
            self.button_v_counters.Hide()

        self.__set_properties()
        self.__do_layout()
	self.__build_experiment()
        
	self.Bind(wx.EVT_BUTTON, self.on_click_button_v_counters, self.button_v_counters)
	self.Bind(wx.EVT_BUTTON, self.on_click_add_exp, self.button_add_exp)
	self.Bind(wx.EVT_BUTTON, self.on_click_rem_exp, self.button_rem_exp)
	self.Bind(wx.EVT_BUTTON, self.on_click_prev, self.button_prev)
	self.Bind(wx.EVT_BUTTON, self.on_click_next, self.button_next)
	self.Bind(wx.EVT_CLOSE, self.on_close_frame)

    def __set_properties(self):
        self.SetTitle("PMCTrack-GUI v" + self.version + " - " + _("Counters & metrics configuration"))
	self.button_v_counters.SetMinSize((210, 28))
	self.button_add_exp.SetMinSize((149, 28))
        self.button_rem_exp.SetMinSize((149, 28))
        self.button_prev.SetMinSize((160, 42))
        self.button_next.SetMinSize((160, 42))

    def __do_layout(self):
	sizer_buttons_above = wx.BoxSizer(wx.HORIZONTAL)
	self.sizer_v_counters_staticbox.Lower()
        sizer_controls = wx.BoxSizer(wx.HORIZONTAL)
        
	sizer_buttons_above.Add(self.button_v_counters, 0, wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL, 5)
        sizer_buttons_above.Add(self.button_add_exp, 0, wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL, 5)
        sizer_buttons_above.Add(self.button_rem_exp, 0, wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL, 5)
        self.separator.Add(sizer_buttons_above, 0, wx.ALIGN_RIGHT, 0)
	
	table_v_counters = wx.FlexGridSizer(self.num_vc, 2, 0, 0)
	v_counters_descriptions = self.facade_xml.getVirtCountersDesc()
	for i in range(self.num_vc):
		table_v_counters.Add(self.v_counter_checkboxs[i], 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 20)
		table_v_counters.Add(wx.StaticText(self, -1, v_counters_descriptions[i]), 0, wx.ALIGN_CENTER_VERTICAL, 0)
	table_v_counters.AddGrowableCol(0)
	table_v_counters.AddGrowableCol(1)
        self.sizer_v_counters.Add(table_v_counters, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        self.separator.Add(self.sizer_v_counters, 0, wx.ALL | wx.EXPAND, 5)
	self.separator.Show(self.sizer_v_counters, self.show_v_counters, True)
        
	self.separator.Add(self.tab_exps, 18, wx.ALL | wx.EXPAND, 5)
        
	sizer_controls.Add(self.button_prev, 0, wx.RIGHT | wx.BOTTOM, 5)
        sizer_controls.Add(self.button_next, 0, wx.RIGHT | wx.BOTTOM, 5)
        self.separator.Add(sizer_controls, 0, wx.ALIGN_RIGHT, 0)
        
	self.SetSizer(self.separator)
        self.Layout()

    def __build_experiment(self):
	# Create experiment object that contains all information about the experiment
	num_exp = len(self.exps)
	new_exp = PMCGenerateExp(num_exp, self.num_fc, self.num_gc, self.tab_exps, self.facade_xml)
	# Connects object with frame
	self.exps.append(new_exp)
        self.tab_exps.AddPage(new_exp.panel, _("Experiment") + " " + str(num_exp + 1))

    def __save_user_config(self):
        # Deletes any previous experiments and virtual counters configuration.
	del self.user_config.experiments[:]
	del self.user_config.virtual_counters[:]

	for nr_v_counter in range(self.num_vc):
		if self.v_counter_checkboxs[nr_v_counter].GetValue() == 1:
			self.user_config.virtual_counters.append(nr_v_counter)
        
	for exp in self.exps:
		self.user_config.experiments.append(Experiment())
		for i in range(self.num_fc): # Saves activated fixed counters configuration.
			if exp.info_counters[i][0].GetValue() == 1:
				self.user_config.experiments[exp.num_exp].eventsHW.append(HWEvent(i, True))
		for i in range(self.num_fc, self.num_fc + self.num_gc): # Saves activated gp counters configuration.
			if exp.info_counters[i][0].GetValue() == 1:
				code_evt = exp.info_counters[i][4].GetCodeEvent()
				flags = exp.info_counters[i][4].GetFlags()
				self.user_config.experiments[exp.num_exp].eventsHW.append(HWEvent(i, False, code_evt, flags))
                                if exp.info_counters[i][4].GetEBS() != "":
                                    self.user_config.experiments[exp.num_exp].ebs_counter = i
                                    self.user_config.experiments[exp.num_exp].ebs_value = exp.info_counters[i][4].GetEBS()
		for i in range(len(exp.info_metrics)):
			if exp.info_metrics[i][0].GetValue() == 1:
				name_metric = exp.info_metrics[i][0].GetLabel()
				formula_metric = exp.info_metrics[i][1].GetLabel()
				self.user_config.experiments[exp.num_exp].metrics.append(Metric(name_metric, formula_metric))

    def __destroy_assign_events(self):
        # Destroys all Assign Event dialogs previously created.
	for exp in self.exps:
		for i in range(self.num_fc, self.num_fc + self.num_gc):
			if exp.info_counters[i][4] != None:
				exp.info_counters[i][4].Destroy()

    def on_click_button_v_counters(self, event):
	self.show_v_counters = not self.show_v_counters
	if self.show_v_counters:
		self.button_v_counters.SetLabel(_("Hide virtual counters"))
	else:
		self.button_v_counters.SetLabel(_("Show virtual counters"))
	self.separator.Show(self.sizer_v_counters, self.show_v_counters, True)
        self.Layout()
	
    def on_click_add_exp(self, event):
	self.__build_experiment()
	self.tab_exps.SetSelection(len(self.exps)-1)

    def on_click_rem_exp(self, event):
	if len(self.exps) == 1:
		dlg = wx.MessageDialog(parent=None, message=_("This is the only existing experiment and at least there must be one."), 
			caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
        	dlg.ShowModal()
       		dlg.Destroy()
	else:
		index_exp_to_rem = self.tab_exps.GetSelection()
		msg = _("You are about to delete the experiment {0} and all its associated configuration.\n\nAre you sure you wanna do this?.").format(index_exp_to_rem + 1)
                dlg = wx.MessageDialog(parent=None, message=msg, caption=_("Advertisement"), style=wx.YES_NO | wx.ICON_EXCLAMATION)
                if dlg.ShowModal() == wx.ID_YES:
		    self.tab_exps.DeletePage(index_exp_to_rem)
		    self.exps.pop(index_exp_to_rem)
		    # Puts the correct number to each experiment to the right of the deleted.
		    for i in range(index_exp_to_rem, len(self.exps)):
		    	self.tab_exps.SetPageText(i, _("Experiment") + " " + str(i + 1))
		    	self.exps[i].num_exp = i
		
    def on_click_prev(self, event):
        self.prev_frame.SetPosition(self.GetPosition())
        self.prev_frame.SetSize(self.GetSize())
        self.Hide()
        self.prev_frame.Show()

    def on_click_next(self, event):
        # Checks that all experiments have at least set a metric.
	all_exps_with_metric = True
	for exp in self.exps:
		if len(exp.info_metrics) < 1:
			all_exps_with_metric = False
		else:
			all_exps_with_metric = False
			for info_metric in exp.info_metrics:
				if info_metric[0].GetValue() == 1:
					all_exps_with_metric = True
		if not all_exps_with_metric:
			break
	
	if not all_exps_with_metric:
		dlg = wx.MessageDialog(parent=None, message=_("All experiments must be at least one metric."), 
			caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
        	dlg.ShowModal()
       		dlg.Destroy()
	else:
		self.__save_user_config()
        	if self.next_frame == None:
	    		self.next_frame = PMCFrameFinalConf(None, -1, "", version=self.version, user_config=self.user_config, facade_xml=self.facade_xml)
			self.next_frame.prev_frame = self
		else:
			self.next_frame.UpdateCtrlBenchmark()
        	self.next_frame.SetPosition(self.GetPosition())
        	self.next_frame.SetSize(self.GetSize())
        	self.Hide()
		self.next_frame.Show()

    def on_close_frame(self, event):
	if self.prev_frame != None:
		self.prev_frame.next_frame = None
		self.prev_frame.Close()
	if self.next_frame != None:
		self.next_frame.prev_frame = None
		self.next_frame.Close()	
        self.__destroy_assign_events()
	self.Destroy()
