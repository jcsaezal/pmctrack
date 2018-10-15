# -*- coding: utf-8 -*-

#
# experiment_tabpanel.py
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
import wx.lib.scrolledpanel as scrolled
from backend.facade_xml import *
from frontend.assign_event_dialog import *

class ExperimentTabPanel():
    def __init__(self, num_exp, num_fixed_counters, num_general_counters, tab_exps, facade_xml):
	self.facade_xml = facade_xml
	self.num_exp = num_exp # Experiment number assigned to this experiment.
	self.num_fc = num_fixed_counters
	self.num_gc = num_general_counters
	self.panel = wx.Panel(tab_exps, -1)
        self.panel_counters = scrolled.ScrolledPanel(self.panel, -1)
        self.panel_metrics = scrolled.ScrolledPanel(self.panel, -1)
	self.info_counters = [] # It contains graphic controls (Labels, checkbox...) counters so that they can later modify.
	self.info_metrics = [] # It contains graphic controls (Labels, checkbox...) metrics so that they can later modify.
	fixed_pmcs = self.facade_xml.getFixedPMCs()
	for i in range(self.num_fc):
		self.info_counters.append([])
		self.info_counters[i].append(CheckBoxNumber(self.panel_counters, -1, "pmc" + str(i), i))
        	self.info_counters[i].append(wx.StaticText(self.panel_counters, -1, _("Fixed-function counter")))
        	self.info_counters[i].append(wx.StaticText(self.panel_counters, -1, fixed_pmcs[i].name))
        	self.info_counters[i].append(wx.Button(self.panel_counters, -1, _("Fixed")))
        	self.info_counters[i][3].SetMinSize((115, 33))
        	self.info_counters[i][3].Enable(False)
		self.info_counters[i][0].Bind(wx.EVT_CHECKBOX, self.on_click_checkbox_number)
	
	for i in range(self.num_fc, self.num_fc + self.num_gc):
		self.info_counters.append([])
		self.info_counters[i].append(CheckBoxNumber(self.panel_counters, -1, "pmc" + str(i), i))
        	self.info_counters[i].append(wx.StaticText(self.panel_counters, -1, _("General purpose counter")))
        	self.info_counters[i].append(wx.StaticText(self.panel_counters, -1, _("No event assigned")))
        	self.info_counters[i].append(ButtonNumber(self.panel_counters, -1, _("Assign event"), i))
		self.info_counters[i].append(None) # In gp counters, this extra position contains screen Assign event to counter (it is created on demand).
        	self.info_counters[i][3].SetMinSize((115, 33))
		self.info_counters[i][0].Bind(wx.EVT_CHECKBOX, self.on_click_checkbox_number)
		self.info_counters[i][3].Bind(wx.EVT_BUTTON, self.on_click_button_number)
	
	self.sizer_counters_staticbox = wx.StaticBox(self.panel, -1, _("Hardware counters configuration"))
        self.sizer_metrics_staticbox = wx.StaticBox(self.panel, -1, _("Metrics configuration"))
	self.label_no_metrics = wx.StaticText(self.panel_metrics, -1, _("There is no any metric for this experiment yet. Use the form below to add one."))
	
	# List of metrics, it is part of the frame because must be modified at runtime.
        self.list_metrics = wx.FlexGridSizer(1, 3, 0, 0)
        self.list_metrics.AddGrowableCol(0)
        self.list_metrics.AddGrowableCol(1)
	
	#New metric form
        self.label_name_new_metric = wx.StaticText(self.panel, -1, " " + _("Name") + ": ")
        self.text_name_new_metric = wx.TextCtrl(self.panel, -1, "", style=wx.TE_PROCESS_ENTER)
	self.text_name_new_metric.Bind(wx.EVT_TEXT_ENTER, self.on_add_metric)
        self.label_formula_new_metric = wx.StaticText(self.panel, -1, _("Formula") + ": ")
        self.text_formula_new_metric = wx.TextCtrl(self.panel, -1, "", style=wx.TE_PROCESS_ENTER)
	self.text_formula_new_metric.Bind(wx.EVT_TEXT_ENTER, self.on_add_metric)
	bmpInfoFormula = wx.ArtProvider.GetBitmap(wx.ART_INFORMATION, wx.ART_OTHER, (16, 16))
	self.bb_info_formula = wx.BitmapButton(self.panel, -1, wx.ArtProvider.GetBitmap(wx.ART_INFORMATION, wx.ART_OTHER, (18, 18)), style=wx.NO_BORDER)
	self.bb_info_formula.SetMinSize((29, 31))
	self.bb_info_formula.Bind(wx.EVT_BUTTON, self.on_click_info_formula)
        self.button_add_metric = wx.Button(self.panel, -1, _("Add metric"))
        self.button_add_metric.SetMinSize((115, 33))
	self.button_add_metric.Bind(wx.EVT_BUTTON, self.on_add_metric)
	
	self.__do_layout()
	
	
    def __do_layout(self):
	sizer_counters_metrics = wx.BoxSizer(wx.VERTICAL)
        self.sizer_metrics_staticbox.Lower()
        sizer_metrics = wx.StaticBoxSizer(self.sizer_metrics_staticbox, wx.VERTICAL)
        sizer_new_metric = wx.BoxSizer(wx.HORIZONTAL)
        self.sizer_counters_staticbox.Lower()
        sizer_counters = wx.StaticBoxSizer(self.sizer_counters_staticbox, wx.HORIZONTAL)
	table_counters = wx.FlexGridSizer(self.num_fc + self.num_gc, 4, 0, 0)
	
	for i in range(self.num_fc + self.num_gc):
		table_counters.Add(self.info_counters[i][0], 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 20)
        	table_counters.Add(self.info_counters[i][1], 0, wx.ALIGN_CENTER_VERTICAL, 0)
        	table_counters.Add(self.info_counters[i][2], 0, wx.ALIGN_CENTER_VERTICAL, 0)
        	table_counters.Add(self.info_counters[i][3], 0, wx.ALIGN_CENTER_VERTICAL, 0)
	
	table_counters.AddGrowableCol(1)
        table_counters.AddGrowableCol(2)
        sizer_counters.Add(self.panel_counters, 1, wx.EXPAND, 0)
        sizer_counters_metrics.Add(sizer_counters, 3, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
       
	self.list_metrics.Add(self.label_no_metrics, 0, wx.LEFT | wx.TOP, 7)
	sizer_metrics.Add(self.panel_metrics, 1, wx.EXPAND, 0)
        sizer_new_metric.Add(self.label_name_new_metric, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        sizer_new_metric.Add(self.text_name_new_metric, 4, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 10)
        sizer_new_metric.Add(self.label_formula_new_metric, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        sizer_new_metric.Add(self.text_formula_new_metric, 4, wx.ALIGN_CENTER_VERTICAL, 0)
	sizer_new_metric.Add(self.bb_info_formula, 0, wx.RIGHT | wx.ALIGN_CENTER_VERTICAL, 10)
        sizer_new_metric.Add(self.button_add_metric, 0, 0, 0)
        sizer_metrics.Add(sizer_new_metric, 0, wx.EXPAND | wx.TOP, 5)

        sizer_counters_metrics.Add(sizer_metrics, 2, wx.LEFT | wx.RIGHT | wx.BOTTOM | wx.EXPAND, 5)
        self.panel_counters.SetSizer(table_counters)
        self.panel_counters.SetAutoLayout(1)
        self.panel_counters.SetupScrolling()
        self.panel_metrics.SetSizer(self.list_metrics)
        self.panel_metrics.SetAutoLayout(1)
        self.panel_metrics.SetupScrolling()
        self.panel.SetSizer(sizer_counters_metrics)


    def on_click_button_number(self, event):
	num_counter = event.GetEventObject().GetNumber()

        # If Assign event dialog not yet been loaded into memory, we loaded.
	if(self.info_counters[num_counter][4] == None):
		self.info_counters[num_counter][4] = AssignEventDialog(None, -1, "", facade_xml=self.facade_xml)
		self.info_counters[num_counter][4].SetLabelTitle(_("Assign event to") + " pmc" + str(num_counter))

	if self.info_counters[num_counter][4].ShowModal() == 0: # It's OK.
		self.info_counters[num_counter][0].SetValue(1)
		self.info_counters[num_counter][2].SetLabel(self.info_counters[num_counter][4].GetStringEvent())
		self.info_counters[num_counter][3].SetLabel(_("Change event"))
		self.info_counters[num_counter][4].SetLabelTitle(_("Change event to") + " pmc" + str(num_counter))

    def on_click_checkbox_number(self, event):
	num_pmc = event.GetEventObject().GetNumber()
        # Prevents activate a general purpose counter that it has not been assigned an event.
	if event.GetEventObject().GetValue() == 1 and len(self.info_counters[num_pmc]) == 5 and self.info_counters[num_pmc][4] == None:
		event.GetEventObject().SetValue(0)
		dlg = wx.MessageDialog(parent=None, message=_("In order to enable a counter, one event has to be assigned first."), 
			caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
        	dlg.ShowModal()
       		dlg.Destroy()

    def on_add_metric(self, event):
	self.AddMetric(self.text_name_new_metric.GetValue(), self.text_formula_new_metric.GetValue())	
	self.text_name_new_metric.SetValue("")
	self.text_formula_new_metric.SetValue("")

    def on_rem_metric(self, event):
	num_metric_to_rem = event.GetEventObject().GetNumber()
	for i in range(3):
		self.list_metrics.Remove(num_metric_to_rem * 3)
		self.info_metrics[num_metric_to_rem][i].Destroy()
	self.info_metrics.pop(num_metric_to_rem)
	total_metrics = len(self.info_metrics)
	# Update the indexes of the 'Delete' button subsequent to the deleted metrics
	for i in range(num_metric_to_rem, total_metrics):
		self.info_metrics[i][2].SetNumber(i)
	# If no metrics show the information label.
	if total_metrics == 0:
		self.label_no_metrics.Show()
		self.list_metrics.Add(self.label_no_metrics, 0, wx.LEFT | wx.TOP, 7)
	else:
		self.list_metrics.SetRows(self.list_metrics.GetRows() - 1)
	self.list_metrics.Layout()
        self.panel_metrics.Layout()
        self.panel_metrics.SetupScrolling()

    def on_click_info_formula(self, event):
	text = _("The formula of a metric must consist of variables (some of the activated hardware counters such pmc0, pmc1, etc; or virtual counters such virt0), constants and arithmetic operations (multiplication *, division /, plus + and minus -).\n\nThese are some valid examples:")
        examples = "pmc0 / (pmc1 * 100)\n(pmc4 + pmc1) / pmc2\npmc3 / virt0"
	dlg = wx.MessageDialog(parent=None, message=text + "\n\n" + examples,
                        caption=_("Help - Format formula"), style=wx.OK|wx.ICON_INFORMATION)
	dlg.ShowModal()
	dlg.Destroy()

    def AddMetric(self, name, formula):
	num_metric = len(self.info_metrics)
        # If no metric the is label information, we take it out and hide layout before adding metrics.
	if num_metric == 0:
		self.list_metrics.Detach(0)
		self.label_no_metrics.Hide()
	else:
		self.list_metrics.SetRows(self.list_metrics.GetRows() + 1)
	
	self.info_metrics.append([])
	if name != "":
		self.info_metrics[num_metric].append(wx.CheckBox(self.panel_metrics, -1, name))
	else:
		self.info_metrics[num_metric].append(wx.CheckBox(self.panel_metrics, -1, _("Metric") + " " + str(num_metric + 1)))
       	self.info_metrics[num_metric].append(wx.StaticText(self.panel_metrics, -1, formula))
       	self.info_metrics[num_metric].append(ButtonNumber(self.panel_metrics, -1, _("Remove metric"), num_metric))
       	self.info_metrics[num_metric][0].SetValue(1)
	self.info_metrics[num_metric][2].SetMinSize((115, 33))
	self.info_metrics[num_metric][2].Bind(wx.EVT_BUTTON, self.on_rem_metric)
	
	self.list_metrics.Add(self.info_metrics[num_metric][0], 0, wx.ALIGN_CENTER_VERTICAL, 0)
       	self.list_metrics.Add(self.info_metrics[num_metric][1], 0, wx.ALIGN_CENTER_VERTICAL, 0)
       	self.list_metrics.Add(self.info_metrics[num_metric][2], 0, 0, 0)
	self.list_metrics.Layout()
        self.panel_metrics.Layout()
        self.panel_metrics.SetupScrolling()

    def DestroyComponents(self):
    	for i in range(self.num_fc, self.num_fc + self.num_gc):
		if self.info_counters[i][4] != None:
    			self.info_counters[i][4].Destroy()
	#self.panel_counters.Destroy()
	#self.panel_metrics.Destroy()
	#self.panel.Destroy()
    
# wx.Button standard class which has been added a numerical attribute (normally be used to store the counter number corresponding to the button)
class ButtonNumber(wx.Button):
    def __init__(self, parent, id, label, number):
	wx.Button.__init__(self, parent, id, label)
	self.number = number

    def GetNumber(self):
	return self.number

    def SetNumber(self, number):
	self.number = number

# wx.CheckBox standard class which has been added a numerical attribute (normally be used to store the counter number corresponding to the checkbox)
class CheckBoxNumber(wx.CheckBox):
    def __init__(self, parent, id, label, number):
	wx.CheckBox.__init__(self, parent, id, label)
	self.number = number

    def GetNumber(self):
	return self.number

    def SetNumber(self, number):
	self.number = number
