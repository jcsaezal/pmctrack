# -*- coding: utf-8 -*-

#
# monitoring_frame.py
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
#
#  2015-05-16 Modified by Abel Serrano to improve translations.
#

import wx
import matplotlib
matplotlib.use('WXAgg')
matplotlib.rcParams.update({'font.size': 9})
from matplotlib.figure import Figure
from matplotlib.backends.backend_wxagg import FigureCanvasWxAgg as FigCanvas
import numpy as np

from backend.user_config import *
from frontend.final_config_panel import *
from frontend.graph_style_dialog import GraphStyleDialog

class MonitoringFrame(wx.Frame):
    def __init__(self, *args, **kwargs):
	kwds = {"style": wx.DEFAULT_FRAME_STYLE}
        wx.Frame.__init__(self, *args, **kwds)

	self.version = kwargs.get("version")
        self.final_panel = kwargs.get("final_panel")
	self.user_config = kwargs.get("user_config")
	self.pack = kwargs.get("pack", "")
	self.num_exp = kwargs.get("num_exp", 0)
	self.num_metric = kwargs.get("num_metric", 0)
	self.name_benchmark = self.user_config.path_benchmark.split("/")[-1].split()[0]

        # Indicates whether the graph is shown fully or only partly present.
        self.show_complete = False

        # Indicates to user how to store the data, if per thread (PID) or per CPU.
        if self.user_config.system_wide:
            self.name_pack = "CPU"
        else:
            self.name_pack = "PID"
	
	#Indicate min and max value of the graph that is being displayed.
	self.minval = float("inf")
	self.maxval = 0

        # Add this frame to the list of monitoring frames that are in memory.
	self.final_panel.mon_frames.append(self)
	
	# Indicates painted samples of a experiment of a particular thread (or CPU in system-wide mode).
	self.samples_draw = []
	for i in range(len(self.user_config.experiments)):
		self.samples_draw.append({})

	exps = []
	for i in range(len(self.user_config.experiments)):
		exps.append(_("Experiment") + " " + str(i + 1))
        self.combo_exp = wx.ComboBox(self, wx.ID_ANY, choices=exps, style=wx.CB_DROPDOWN | wx.CB_READONLY)

	metrics = []
	for metric in self.user_config.experiments[self.num_exp].metrics:
		metrics.append(metric.name)
        self.combo_metric = wx.ComboBox(self, wx.ID_ANY, choices=metrics, style=wx.CB_DROPDOWN | wx.CB_READONLY)

        self.combo_pack = wx.ComboBox(self, wx.ID_ANY, choices=sorted(self.final_panel.pmc_extract.data.keys()), style=wx.CB_DROPDOWN | wx.CB_READONLY)
	self.label_pack = wx.StaticText(self, -1, self.name_pack + ": ")
        self.label_exp = wx.StaticText(self, -1, _("Experiment") + ": ")
        self.label_metric = wx.StaticText(self, -1, _("Metric") + ": ")
        self.button_this_window = wx.Button(self, wx.ID_ANY, _("Show graph"))
        self.button_other_window = wx.Button(self, wx.ID_ANY, _("Open up graph in a new window"))
        self.sizer_sel_graph_staticbox = wx.StaticBox(self, wx.ID_ANY, _("Select graph to show"))
        self.sizer_graph_staticbox = wx.StaticBox(self, wx.ID_ANY, _("Waiting monitoring data..."))
        self.button_change_vis_graph = wx.Button(self, wx.ID_ANY, _("Show complete graph"))
        self.button_screenshot = wx.Button(self, wx.ID_ANY, _("Take graph screenshot"))
        self.button_hide_controls = wx.Button(self, wx.ID_ANY, _("Hide controls"))
	self.button_show_controls = wx.Button(self, wx.ID_ANY, _("Show controls"))
        self.button_stop_monitoring = wx.Button(self, wx.ID_ANY, _("Stop application"))
        self.sizer_options_staticbox = wx.StaticBox(self, wx.ID_ANY, _("Options")) 
        self.graph_style_dialog = GraphStyleDialog(None, -1, "")
	self.timer_update_data = wx.Timer(self)

        self.separator = wx.BoxSizer(wx.VERTICAL)
        self.sizer_sel_graph = wx.StaticBoxSizer(self.sizer_sel_graph_staticbox, wx.VERTICAL)
        self.sizer_options = wx.StaticBoxSizer(self.sizer_options_staticbox, wx.VERTICAL)

        self.__init_plot()
        self.__set_properties()
        self.__do_layout()

	self.Bind(wx.EVT_TIMER, self.on_timer_update_data, self.timer_update_data)        
	self.Bind(wx.EVT_COMBOBOX, self.on_change_experiment, self.combo_exp)        
	self.Bind(wx.EVT_BUTTON, self.on_click_this_window, self.button_this_window)
	self.Bind(wx.EVT_BUTTON, self.on_click_other_window, self.button_other_window)
	self.Bind(wx.EVT_BUTTON, self.on_click_change_vis_graph, self.button_change_vis_graph)
	self.Bind(wx.EVT_BUTTON, self.on_click_screenshot, self.button_screenshot)
	self.Bind(wx.EVT_BUTTON, self.on_click_hide_controls, self.button_hide_controls)
	self.Bind(wx.EVT_BUTTON, self.on_click_show_controls, self.button_show_controls)
	self.Bind(wx.EVT_BUTTON, self.on_click_stop_monitoring, self.button_stop_monitoring)
	self.Bind(wx.EVT_CLOSE, self.on_close_frame)
        self.fig.canvas.mpl_connect('button_press_event', self.on_click_graph)

    def __set_properties(self):
        self.SetTitle("PMCTrack-GUI v" + self.version + " - " + _("Monitoring application '{0}'").format(self.name_benchmark))
        self.SetSize((750, 640))
	if self.pack != "":
		self.combo_pack.SetStringSelection(self.pack)
	        metric = self.user_config.experiments[self.num_exp].metrics[self.num_metric].name.encode("utf-8")
		self.sizer_graph_staticbox.SetLabel(_("Showing graph with {0} {1}, experiment {2} and metric '{3}'").format(self.name_pack, self.pack, (self.num_exp + 1), metric))
	else:
		self.button_this_window.Disable()
		self.button_other_window.Disable()
        
	self.combo_exp.SetSelection(self.num_exp)
	self.combo_metric.SetSelection(self.num_metric)
        self.button_this_window.SetMinSize((100, 30))
        self.button_other_window.SetMinSize((100, 30))
        self.button_change_vis_graph.SetMinSize((100, 30))
        self.button_screenshot.SetMinSize((100, 30))
        self.button_hide_controls.SetMinSize((100, 30))
        self.button_stop_monitoring.SetMinSize((100, 30))
        
        if self.user_config.graph_style.mode_number >= 0:
            self.graph_style_dialog.SetModeNumber(self.user_config.graph_style.mode_number)
        else:
            self.graph_style_dialog.SetCustomizedMode(self.user_config.graph_style.bg_color, self.user_config.graph_style.grid_color, self.user_config.graph_style.line_color, self.user_config.graph_style.line_style_number, self.user_config.graph_style.line_width)

        self.timer_update_data.Start(100)

    def __do_layout(self):
        sizer_options_graph = wx.BoxSizer(wx.HORIZONTAL)
        sizer_options_graph2 = wx.BoxSizer(wx.HORIZONTAL)
        self.sizer_options_staticbox.Lower()
        self.sizer_graph_staticbox.Lower()
        sizer_graph = wx.StaticBoxSizer(self.sizer_graph_staticbox, wx.VERTICAL)
        self.sizer_sel_graph_staticbox.Lower()
	grid_sizer_sel_graph = wx.FlexGridSizer(3, 2, 5, 0)
        grid_sizer_sel_graph.AddGrowableCol(1)
        sizer_buttons_sel_graph = wx.BoxSizer(wx.HORIZONTAL)
        grid_sizer_sel_graph.Add(self.label_pack, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_sel_graph.Add(self.combo_pack, 0, wx.EXPAND, 0)
        grid_sizer_sel_graph.Add(self.label_exp, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_sel_graph.Add(self.combo_exp, 0, wx.EXPAND, 0)
        grid_sizer_sel_graph.Add(self.label_metric, 0, wx.LEFT | wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_sel_graph.Add(self.combo_metric, 0, wx.EXPAND, 0)
        sizer_buttons_sel_graph.Add(self.button_other_window, 1, wx.RIGHT, 2)
        sizer_buttons_sel_graph.Add(self.button_this_window, 1, wx.LEFT, 3)
	self.sizer_sel_graph.Add(grid_sizer_sel_graph, 0, wx.ALL | wx.EXPAND, 5)
        self.sizer_sel_graph.Add(sizer_buttons_sel_graph, 0, wx.RIGHT | wx.LEFT | wx.BOTTOM | wx.EXPAND, 5)
        self.separator.Add(self.sizer_sel_graph, 0, wx.ALL | wx.EXPAND, 5)
        sizer_graph.Add(self.canvas, 1, wx.EXPAND, 0)
        self.separator.Add(sizer_graph, 1, wx.ALL | wx.EXPAND, 4)
        sizer_options_graph.Add(self.button_change_vis_graph, 1, wx.RIGHT, 2)
        sizer_options_graph.Add(self.button_screenshot, 1, wx.LEFT, 3)
	self.sizer_options.Add(sizer_options_graph, 1, wx.ALL | wx.EXPAND, 5)
	sizer_options_graph2.Add(self.button_hide_controls, 1, wx.RIGHT, 2)
        sizer_options_graph2.Add(self.button_stop_monitoring, 1, wx.LEFT, 3)
	self.sizer_options.Add(sizer_options_graph2, 1, wx.RIGHT | wx.BOTTOM | wx.LEFT | wx.EXPAND, 5)
        self.separator.Add(self.sizer_options, 0, wx.ALL | wx.EXPAND, 5)
        self.separator.Add(self.button_show_controls, 0, wx.EXPAND, 0)
	self.separator.Show(self.button_show_controls, False, False)
        self.SetSizer(self.separator)
        self.Layout()

    def __init_plot(self):
        self.dpi = 100
        self.fig = Figure((3.0, 3.0), dpi=self.dpi)
        self.fig.subplots_adjust(left=0.07, right=0.93, top=0.92, bottom=0.13) # Adjust the chart to occupy as much space the canvas.

        self.axes = self.fig.add_subplot(111)
        self.axes.set_axis_bgcolor(self.user_config.graph_style.bg_color)
	self.axes.set_ylabel(self.user_config.experiments[self.num_exp].metrics[self.num_metric].name)
	self.axes.set_xlabel(_("Samples"))
       	self.axes.grid(True, color=self.user_config.graph_style.grid_color)

        self.plot_data = self.axes.plot([])[0]
	self.plot_data.set_linewidth(self.user_config.graph_style.line_width)
	self.plot_data.set_linestyle(self.user_config.graph_style.line_style)
	self.plot_data.set_color(self.user_config.graph_style.line_color)
        self.canvas = FigCanvas(self, -1, self.fig) 

    def __draw_plot(self):
	graph_data = self.final_panel.pmc_extract.data[self.pack][self.num_exp][self.num_metric]
	
	# Updates the minimum and maximum value of the graph that is displayed (only if it's worth)
	if len(graph_data[self.samples_draw[self.num_exp].get(self.pack, 0):]) > 0:
		self.minval = min(self.minval, min(graph_data[self.samples_draw[self.num_exp].get(self.pack, 0):]))
		self.maxval = max(self.maxval, max(graph_data[self.samples_draw[self.num_exp].get(self.pack, 0):]))

	# Updates X and Y axes range.
	xmax = len(graph_data) - 1
	if self.show_complete and xmax > 50:
		xmin = 0
		ymin = self.minval
		ymax = self.maxval
       	else:
		xmin = xmax - 50
		ymin = min(graph_data[-50:])
		ymax = max(graph_data[-50:])
	
	# Adds small top and bottom margins to Y axe.
        margin = ((ymax - ymin) * 0.1) + 0.01
        ymin -= margin
        ymax += margin

       	self.axes.set_xbound(lower=xmin, upper=xmax)
       	self.axes.set_ybound(lower=ymin, upper=ymax)	
       	
       	self.plot_data.set_xdata(np.arange(len(graph_data)))
       	self.plot_data.set_ydata(np.array(graph_data))
       	
       	self.canvas.draw()

    def __change_vis_controls(self, show):
	self.separator.Show(self.sizer_sel_graph, show, True)
	self.separator.Show(self.sizer_options, show, True)
	self.separator.Show(self.button_show_controls, not show, False)
	self.separator.Layout()
	
    def on_timer_update_data(self, event):
	packs_count = self.combo_pack.GetCount()
	# If a new pack (pid or cpu) is detected update the corresponding graphical controls.
	if len(self.final_panel.pmc_extract.data) > packs_count:
		self.combo_pack.SetItems(sorted(self.final_panel.pmc_extract.data))
		if packs_count > 0:
			self.combo_pack.SetStringSelection(self.pack)
		else: # If the first pack (pid or cpu) is detected as what we are currently monitoring pack.
			self.combo_pack.SetSelection(0)
			self.pack = self.combo_pack.GetStringSelection()
	        	metric = self.user_config.experiments[self.num_exp].metrics[self.num_metric].name.encode("utf-8")
		        self.sizer_graph_staticbox.SetLabel(_("Showing graph with {0} {1}, experiment {2} and metric '{3}'").format(self.name_pack, self.pack, (self.num_exp + 1), metric))
			self.button_this_window.Enable()
			self.button_other_window.Enable()

	if self.pack != "":
		num_graph_data = len(self.final_panel.pmc_extract.data[self.pack][self.num_exp][self.num_metric])
		
		# Only if there are outstanding paint painting to the pack (pid or cpu) being displayed data.
		if num_graph_data > self.samples_draw[self.num_exp].get(self.pack, 0):
        		self.__draw_plot()
			self.samples_draw[self.num_exp][self.pack] = num_graph_data

		if self.final_panel.pmc_extract.state == 'F':
			self.timer_update_data.Stop()
			self.SetTitle(self.GetTitle() + " " + _("(finished)"))
			self.button_stop_monitoring.Disable()
			if len(self.final_panel.mon_frames) > 0 and self.final_panel.mon_frames[0] == self:
				self.final_panel.button_monitoring.SetLabel(_("Close monitoring windows"))
				dlg = wx.MessageDialog(parent=None, message=_("The application '{0}' is done.").format(self.name_benchmark), 
					caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
        			dlg.ShowModal()
       				dlg.Destroy()
		elif self.final_panel.pmc_extract.state == 'S':
			self.timer_update_data.Stop()
			self.SetTitle(self.GetTitle() + " " + _("(stopped)"))
        	    	self.button_stop_monitoring.SetLabel(_("Resume application"))

    def on_click_this_window(self, event):
	self.pack = self.combo_pack.GetStringSelection()
	self.num_exp = self.combo_exp.GetSelection()
	self.num_metric = self.combo_metric.GetSelection()
	metric = self.user_config.experiments[self.num_exp].metrics[self.num_metric].name.encode('utf-8')
	self.sizer_graph_staticbox.SetLabel(_("Showing graph with {0} {1}, experiment {2} and metric '{3}'").format(self.name_pack, self.pack, (self.num_exp + 1), metric))
	self.axes.set_ylabel(metric.decode('utf-8'))
	graph_data = self.final_panel.pmc_extract.data[self.pack][self.num_exp][self.num_metric]
	if self.samples_draw[self.num_exp].get(self.pack, 0) > 0:
		self.minval = min(graph_data[0:self.samples_draw[self.num_exp][self.pack]])
		self.maxval = max(graph_data[0:self.samples_draw[self.num_exp][self.pack]])
	else:
		self.minval = float("inf")
		self.maxval = 0
        self.__draw_plot()

    def on_click_other_window(self, event):
	sel_pack = self.combo_pack.GetStringSelection()
	sel_exp = self.combo_exp.GetSelection()
	sel_met = self.combo_metric.GetSelection()
	mon_frame = MonitoringFrame(None, -1, "", version=self.version, final_panel=self.final_panel, user_config=self.user_config, pack=sel_pack, num_exp=sel_exp, num_metric=sel_met)
    	mon_frame.Show()

    def on_change_experiment(self, event):
	metrics = []
	for metric in self.user_config.experiments[self.combo_exp.GetSelection()].metrics:
		metrics.append(metric.name)
        self.combo_metric.SetItems(metrics)
	self.combo_metric.SetSelection(0)

    def on_click_graph(self, event):
        if self.fig.canvas.HasCapture():
            self.fig.canvas.ReleaseMouse()
        if self.graph_style_dialog.ShowModal() == 0:
            self.axes.set_axis_bgcolor(self.graph_style_dialog.GetBgColor())
       	    self.axes.grid(True, color=self.graph_style_dialog.GetGridColor())
	    self.plot_data.set_linewidth(self.graph_style_dialog.GetLineWidth())
	    self.plot_data.set_linestyle(self.graph_style_dialog.GetLineStyle())
	    self.plot_data.set_color(self.graph_style_dialog.GetLineColor())
            self.__draw_plot()

    def on_click_change_vis_graph(self, event):
        self.show_complete = not self.show_complete
        if self.show_complete:
            self.button_change_vis_graph.SetLabel(_("Show partial graph"))
        else:
            self.button_change_vis_graph.SetLabel(_("Show complete graph"))
	self.__draw_plot()

    def on_click_stop_monitoring(self, event):
	if self.final_panel.pmc_extract.state == 'R':
		self.final_panel.pmc_extract.StopMonitoring()
	elif self.final_panel.pmc_extract.state == 'S':
		self.final_panel.pmc_extract.ResumeMonitoring()
		for mon_frame in self.final_panel.mon_frames:
        		mon_frame.timer_update_data.Start(100)
            		mon_frame.button_stop_monitoring.SetLabel(_("Stop application"))
        		mon_frame.SetTitle("PMCTrack-GUI v" + self.version + " - " + _("Monitoring application '{0}'").format(self.name_benchmark))

    def on_click_screenshot(self, event):
	metric = self.user_config.experiments[self.num_exp].metrics[self.num_metric].name
	file_choices = "PNG (*.png)|*.png"
        
        dlg = wx.FileDialog(
            self, 
            message=_("Save graph screenshot as..."),
            defaultDir=os.getcwd(),
            defaultFile="{0}-{1}-{2}.png".format(self.pack, self.num_exp, metric.replace(" ", "_")),
            wildcard=file_choices,
            style=wx.SAVE)
        
        if dlg.ShowModal() == wx.ID_OK:
            path = dlg.GetPath()
            self.canvas.print_figure(path, dpi=self.dpi)

    def on_click_hide_controls(self, event):
	self.__change_vis_controls(False)

    def on_click_show_controls(self, event):
	self.__change_vis_controls(True)

    def on_close_frame(self, event):
	if len(self.final_panel.mon_frames) > 1:
		self.final_panel.mon_frames.remove(self)
                self.graph_style_dialog.Destroy()
		self.Destroy()
	else:
		self.final_panel.StopMonitoring()
