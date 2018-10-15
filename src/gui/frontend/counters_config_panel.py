# -*- coding: utf-8 -*-

#
# counters_config_panel.py
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
import xml.etree.ElementTree as ET
from backend.facade_xml import *
from backend.user_config import *
from frontend.experiment_tabpanel import *
from frontend.assign_event_dialog import *

class CountersConfigPanel():
    def __init__(self, config_frame):
        self.config_frame = config_frame
        self.panel = wx.Panel(self.config_frame, -1)
	(self.num_fc, self.num_gc, self.num_vc) = self.config_frame.facade_xml.getNrCounters()
	self.button_v_counters = wx.Button(self.panel, -1, _("Show virtual counters"))
	self.button_add_exp = wx.Button(self.panel, -1, _("Add experiment"))
        self.button_rem_exp = wx.Button(self.panel, -1, _("Remove experiment"))
	self.sizer_v_counters_staticbox = wx.StaticBox(self.panel, -1, _("Virtual counters available"))
        self.sizer_v_counters = wx.StaticBoxSizer(self.sizer_v_counters_staticbox, wx.VERTICAL)
	self.v_counter_checkboxs = [] # List of graphics controls CheckBox of virtual counters available.
	self.show_v_counters = False # Indicates if shows list of virtual counters.
        self.tab_exps = wx.Notebook(self.panel, -1, style=0)
	self.exps = [] # List of experiments added (objects of ExperimentTabPanel class).
        self.button_prev = wx.Button(self.panel, -1, "< " + _("Back"))
        self.button_next = wx.Button(self.panel, -1, _("Next") + " >")
        self.separator = wx.BoxSizer(wx.VERTICAL)
	
	if self.num_vc > 0:
            for vcounter in range(self.num_vc):    
		self.v_counter_checkboxs.append(wx.CheckBox(self.panel, -1, "virt" + str(vcounter)))
        else:
            self.button_v_counters.Hide()

        self.__set_properties()
        self.__do_layout()
	#self.BuildExperiment()
        
	self.button_v_counters.Bind(wx.EVT_BUTTON, self.on_click_button_v_counters)
	self.button_add_exp.Bind(wx.EVT_BUTTON, self.on_click_add_exp)
	self.button_rem_exp.Bind(wx.EVT_BUTTON, self.on_click_rem_exp)
	self.button_prev.Bind(wx.EVT_BUTTON, self.on_click_prev)
	self.button_next.Bind(wx.EVT_BUTTON, self.on_click_next)

    def __set_properties(self):
	self.button_v_counters.SetMinSize((210, 28))
	self.button_add_exp.SetMinSize((149, 28))
        self.button_rem_exp.SetMinSize((149, 28))
        self.button_prev.SetMinSize((225, 42))
        self.button_next.SetMinSize((225, 42))

    def __do_layout(self):
	sizer_buttons_above = wx.BoxSizer(wx.HORIZONTAL)
	self.sizer_v_counters_staticbox.Lower()
        sizer_controls = wx.BoxSizer(wx.HORIZONTAL)
        
	sizer_buttons_above.Add(self.button_v_counters, 0, wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL, 5)
        sizer_buttons_above.Add(self.button_add_exp, 0, wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL, 5)
        sizer_buttons_above.Add(self.button_rem_exp, 0, wx.RIGHT | wx.TOP | wx.ALIGN_CENTER_HORIZONTAL, 5)
        self.separator.Add(sizer_buttons_above, 0, wx.ALIGN_RIGHT, 0)
	
	table_v_counters = wx.FlexGridSizer(self.num_vc, 2, 0, 0)
	v_counters_descriptions = self.config_frame.facade_xml.getVirtCountersDesc()
	for i in range(self.num_vc):
		table_v_counters.Add(self.v_counter_checkboxs[i], 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 20)
		table_v_counters.Add(wx.StaticText(self.panel, -1, v_counters_descriptions[i]), 0, wx.ALIGN_CENTER_VERTICAL, 0)
	table_v_counters.AddGrowableCol(0)
	table_v_counters.AddGrowableCol(1)
        self.sizer_v_counters.Add(table_v_counters, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        self.separator.Add(self.sizer_v_counters, 0, wx.ALL | wx.EXPAND, 5)
	self.separator.Show(self.sizer_v_counters, self.show_v_counters, True)
        
	self.separator.Add(self.tab_exps, 18, wx.ALL | wx.EXPAND, 5)
        
	sizer_controls.Add(self.button_prev, 0, wx.RIGHT | wx.BOTTOM, 5)
        sizer_controls.Add(self.button_next, 0, wx.RIGHT | wx.BOTTOM, 5)
        self.separator.Add(sizer_controls, 0, wx.ALIGN_RIGHT, 0)
	self.panel.SetSizer(self.separator)

    def BuildExperiment(self):
	# Create experiment object that contains all information about the experiment
	num_exp = len(self.exps)
	new_exp = ExperimentTabPanel(num_exp, self.num_fc, self.num_gc, self.tab_exps, self.config_frame.facade_xml)
	# Connects object with this panel
	self.exps.append(new_exp)
        self.tab_exps.AddPage(new_exp.panel, _("Experiment") + " " + str(num_exp + 1))

    def SaveUserConfig(self):
        # Deletes any previous experiments and virtual counters configuration.
	del self.config_frame.user_config.experiments[:]
	del self.config_frame.user_config.virtual_counters[:]

	for nr_v_counter in range(self.num_vc):
		if self.v_counter_checkboxs[nr_v_counter].GetValue() == 1:
			self.config_frame.user_config.virtual_counters.append(nr_v_counter)
        
	for exp in self.exps:
		self.config_frame.user_config.experiments.append(Experiment())
		for i in range(self.num_fc): # Saves activated fixed counters configuration.
			if exp.info_counters[i][0].GetValue() == 1:
				self.config_frame.user_config.experiments[exp.num_exp].eventsHW.append(HWEvent(i, True))
		for i in range(self.num_fc, self.num_fc + self.num_gc): # Saves activated gp counters configuration.
			if exp.info_counters[i][0].GetValue() == 1:
				code_evt = exp.info_counters[i][4].GetCodeEvent()
				flags = exp.info_counters[i][4].GetFlags()
				self.config_frame.user_config.experiments[exp.num_exp].eventsHW.append(HWEvent(i, False, code_evt, flags))
                                if exp.info_counters[i][4].GetEBS() != "":
                                    self.config_frame.user_config.experiments[exp.num_exp].ebs_counter = i
                                    self.config_frame.user_config.experiments[exp.num_exp].ebs_value = exp.info_counters[i][4].GetEBS()
		for i in range(len(exp.info_metrics)):
			if exp.info_metrics[i][0].GetValue() == 1:
				name_metric = exp.info_metrics[i][0].GetLabel()
				formula_metric = exp.info_metrics[i][1].GetLabel()
				self.config_frame.user_config.experiments[exp.num_exp].metrics.append(Metric(name_metric, formula_metric))

    def ValidateCountersAndMetricsConfig(self):
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
	return all_exps_with_metric

    def on_click_button_v_counters(self, event):
	self.show_v_counters = not self.show_v_counters
	if self.show_v_counters:
		self.button_v_counters.SetLabel(_("Hide virtual counters"))
	else:
		self.button_v_counters.SetLabel(_("Show virtual counters"))
	self.separator.Show(self.sizer_v_counters, self.show_v_counters, True)
        self.panel.Layout()
	
    def on_click_add_exp(self, event):
	self.BuildExperiment()
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
		    self.exps[index_exp_to_rem].DestroyComponents()
		    self.exps.pop(index_exp_to_rem)
		    # Puts the correct number to each experiment to the right of the deleted.
		    for i in range(index_exp_to_rem, len(self.exps)):
		    	self.tab_exps.SetPageText(i, _("Experiment") + " " + str(i + 1))
		    	self.exps[i].num_exp = i
                dlg.Destroy()
		
    def on_click_prev(self, event):
        self.config_frame.GoToPanel(0)

    def on_click_next(self, event):
	if self.ValidateCountersAndMetricsConfig():
                self.config_frame.GoToPanel(2)

    def ConfigToXML(self, xml_root):
        xml_vc = ET.SubElement(xml_root, "virtual_counters")
	for nr_v_counter in range(self.num_vc):
                if self.v_counter_checkboxs[nr_v_counter].GetValue() == 1:
        		ET.SubElement(xml_vc, "num_vc").text = str(nr_v_counter)
    
        xml_exps = ET.SubElement(xml_root, "experiments")
        for exp in self.exps:
        	xml_exp = ET.SubElement(xml_exps, "experiment")
        	xml_fc = ET.SubElement(xml_exp, "fixed_counters")
                for i in range(self.num_fc): # Saves activated fixed counters configuration.
                        if exp.info_counters[i][0].GetValue() == 1:
        			ET.SubElement(xml_fc, "num_fc").text = str(i)
        	xml_gcs = ET.SubElement(xml_exp, "gp_counters")
                for i in range(self.num_fc, self.num_fc + self.num_gc): # Saves activated gp counters configuration.
                        if exp.info_counters[i][0].GetValue() == 1:
        			xml_gc = ET.SubElement(xml_gcs, "gp_counter")
        			ET.SubElement(xml_gc, "num_gc").text = str(i)
        			ET.SubElement(xml_gc, "event_index").text = str(exp.info_counters[i][4].selected_event)
        			
				xml_se = ET.SubElement(xml_gc, "subevents_index")
				for j in exp.info_counters[i][4].list_subevents.GetSelections():
        				ET.SubElement(xml_se, "subevent_index").text = str(j)

        			ET.SubElement(xml_gc, "usr_value").text = str(exp.info_counters[i][4].checkbox_usr.GetValue())
        			ET.SubElement(xml_gc, "os_value").text = str(exp.info_counters[i][4].checkbox_os.GetValue())
        			ET.SubElement(xml_gc, "umask_value").text = exp.info_counters[i][4].text_umask.GetValue()
        			ET.SubElement(xml_gc, "cmask_value").text = exp.info_counters[i][4].text_cmask.GetValue()
        			ET.SubElement(xml_gc, "ebs_value").text = exp.info_counters[i][4].text_ebs.GetValue()
					
        	xml_ms = ET.SubElement(xml_exp, "metrics")
                for i in range(len(exp.info_metrics)):
                        if exp.info_metrics[i][0].GetValue() == 1:
        			xml_m = ET.SubElement(xml_ms, "metric")
                                ET.SubElement(xml_m, "name").text = exp.info_metrics[i][0].GetLabel()
                                ET.SubElement(xml_m, "formula").text = exp.info_metrics[i][1].GetLabel()

    def ConfigFromXML(self, xml_root):
    	v_counters = xml_root.find("virtual_counters")
	for v_counter in v_counters:
		self.v_counter_checkboxs[int(v_counter.text)].SetValue(1)
	
	experiments = xml_root.find("experiments")
	for experiment in experiments:
		self.BuildExperiment()
		num_exp = len(self.exps) - 1
    		f_counters = experiment.find("fixed_counters")
		for f_counter in f_counters:
			self.exps[num_exp].info_counters[int(f_counter.text)][0].SetValue(1)

    		gp_counters = experiment.find("gp_counters")
		for gp_counter in gp_counters:
			num_gc = int(gp_counter.find("num_gc").text)
			self.exps[num_exp].info_counters[num_gc][4] = AssignEventDialog(None, -1, "", facade_xml=self.config_frame.facade_xml)
			event_index = int(gp_counter.find("event_index").text)
			self.exps[num_exp].info_counters[num_gc][4].list_event.SetSelection(event_index)
			self.exps[num_exp].info_counters[num_gc][4].selected_event = event_index
			self.exps[num_exp].info_counters[num_gc][4].UpdateListSubevents()

			subevents_index = gp_counter.find("subevents_index")
			del self.exps[num_exp].info_counters[num_gc][4].selected_subevents[:]
			for subevent_index in subevents_index:
				self.exps[num_exp].info_counters[num_gc][4].list_subevents.SetSelection(int(subevent_index.text))
				self.exps[num_exp].info_counters[num_gc][4].selected_subevents.append(int(subevent_index.text))

			self.exps[num_exp].info_counters[num_gc][4].checkbox_usr.SetValue(gp_counter.find("usr_value").text == "True")
			self.exps[num_exp].info_counters[num_gc][4].checkbox_os.SetValue(gp_counter.find("os_value").text == "True")
			umask_value = gp_counter.find("umask_value").text
			cmask_value = gp_counter.find("cmask_value").text
			ebs_value = gp_counter.find("ebs_value").text
			if umask_value != None:
				self.exps[num_exp].info_counters[num_gc][4].text_umask.SetValue(umask_value)
			if cmask_value != None:
				self.exps[num_exp].info_counters[num_gc][4].text_cmask.SetValue(cmask_value)
			if ebs_value != None:
				self.exps[num_exp].info_counters[num_gc][4].text_ebs.SetValue(ebs_value)

			self.exps[num_exp].info_counters[num_gc][0].SetValue(1)
			self.exps[num_exp].info_counters[num_gc][2].SetLabel(self.exps[num_exp].info_counters[num_gc][4].GetStringEvent())
			self.exps[num_exp].info_counters[num_gc][3].SetLabel(_("Change event"))
			self.exps[num_exp].info_counters[num_gc][4].SetLabelTitle(_("Change event to") + " pmc" + str(num_gc))

    		metrics = experiment.find("metrics")
		for metric in metrics:
			self.exps[num_exp].AddMetric(metric.find("name").text, metric.find("formula").text)
	
    def DestroyComponents(self):
	for exp in self.exps:
		exp.DestroyComponents()
	self.panel.Destroy()
