#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
# pmc_dialog_graph_style.py
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
import matplotlib
import platform

from matplotlib.figure import Figure
from matplotlib.backends.backend_wxagg import FigureCanvasWxAgg as FigCanvas

def GetColorString(color):
	if platform.system() == "Darwin":
		return color
	else:
		return color.GetAsString(flags=wx.C2S_HTML_SYNTAX)

class PMCStyleGraph(wx.Dialog):
    def __init__(self, *args, **kwds):
        kwds["style"] = wx.DEFAULT_FRAME_STYLE
        wx.Dialog.__init__(self, *args, **kwds)
        self.list_modes = wx.ListBox(self, -1, choices=[])
        self.sizer_list_modes_staticbox = wx.StaticBox(self, -1, _("List of graph style modes"))
        self.sizer_preview_staticbox = wx.StaticBox(self, -1, _("Preview"))
        self.label_bg_color = wx.StaticText(self, -1, _("Background color") + ": ")
        self.button_bg_color = wx.ColourPickerCtrl(self, -1, '#FFFFFF')
        self.label_grid_color = wx.StaticText(self, -1, _("Grid color") + ": ")
        self.button_grid_color = wx.ColourPickerCtrl(self, -1, '#666666')
        self.label_line_color = wx.StaticText(self, -1, _("Line color") + ": ")
        self.button_line_color = wx.ColourPickerCtrl(self, -1, '#0000FF')
        self.label_line_style = wx.StaticText(self, -1, _("Line style") + ": ")
        self.combo_line_style = wx.ComboBox(self, -1, choices=[_("Solid"), _("Dashed"), _("Dashdot"), _("Dotted")], style=wx.CB_DROPDOWN | wx.CB_READONLY)
        self.label_line_width = wx.StaticText(self, -1, _("Line width") + ": ")
        self.spin_line_width = wx.SpinCtrl(self, -1, "", min=1, max=10)
        self.sizer_customize_staticbox = wx.StaticBox(self, -1, _("Customize"))
	self.button_apply_style = wx.Button(self, -1, _("Apply graph style"))

	self.dpi = 70
        self.fig = Figure((3.0, 3.0), dpi=self.dpi)
        self.fig.subplots_adjust(left=0.05, right=0.945, top=0.95, bottom=0.06) # Adjust the chart to occupy as much space the canvas
        self.axes = self.fig.add_subplot(111)
        self.axes.set_axis_bgcolor('#FFFFFF')
        self.axes.grid(True, color='#666666')
	example_data_y = [0.1,0.2,0.3,0.5,0.8,1.6,2.5,4,4.5,4.8,4.3,3.5,3.8,4.3,6,7.1,7.6]
	example_data_x = range(17)
        self.plot_data = self.axes.plot([], linewidth=1, linestyle='solid', color='#0000FF')[0]
	self.plot_data.set_xdata(example_data_x)
	self.plot_data.set_ydata(example_data_y)
	self.axes.set_xbound(lower=0, upper=15)
	self.axes.set_ybound(lower=0, upper=8)
        self.canvas = FigCanvas(self, -1, self.fig)

	self.__insert_style_modes()
        self.__set_properties()
        self.__do_layout()

	self.Bind(wx.EVT_LISTBOX, self.on_select_mode, self.list_modes)
	self.Bind(wx.EVT_COLOURPICKER_CHANGED, self.on_change_bg_color, self.button_bg_color)
	self.Bind(wx.EVT_COLOURPICKER_CHANGED, self.on_change_grid_color, self.button_grid_color)
	self.Bind(wx.EVT_COLOURPICKER_CHANGED, self.on_change_line_color, self.button_line_color)
	self.Bind(wx.EVT_COMBOBOX, self.on_change_line_style, self.combo_line_style)
	self.Bind(wx.EVT_SPINCTRL, self.on_change_line_width, self.spin_line_width)
	self.Bind(wx.EVT_BUTTON, self.on_apply_style, self.button_apply_style)

    def __set_properties(self):
        self.SetTitle(_("Graph style configuration"))
        self.SetSize((700, 540))
	self.list_modes.SetSelection(0)
        self.combo_line_style.SetSelection(0)
	self.button_apply_style.SetMinSize((200, 37))

    def __do_layout(self):
	separator = wx.BoxSizer(wx.VERTICAL)
        sizer_div = wx.BoxSizer(wx.HORIZONTAL)
        sizer_subdiv = wx.BoxSizer(wx.VERTICAL)
        self.sizer_customize_staticbox.Lower()
        sizer_customize = wx.StaticBoxSizer(self.sizer_customize_staticbox, wx.HORIZONTAL)
        grid_sizer_customize = wx.FlexGridSizer(5, 2, 5, 0)
        self.sizer_preview_staticbox.Lower()
        sizer_preview = wx.StaticBoxSizer(self.sizer_preview_staticbox, wx.HORIZONTAL)
        self.sizer_list_modes_staticbox.Lower()
        sizer_list_modes = wx.StaticBoxSizer(self.sizer_list_modes_staticbox, wx.HORIZONTAL)
        sizer_list_modes.Add(self.list_modes, 1, wx.ALL | wx.EXPAND, 5)
        sizer_div.Add(sizer_list_modes, 1, wx.ALL | wx.EXPAND, 5)
	sizer_preview.Add(self.canvas, 1, wx.EXPAND, 0)
        sizer_subdiv.Add(sizer_preview, 1, wx.ALL | wx.EXPAND, 5)
        grid_sizer_customize.Add(self.label_bg_color, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_customize.Add(self.button_bg_color, 0, wx.EXPAND, 0)
        grid_sizer_customize.Add(self.label_grid_color, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_customize.Add(self.button_grid_color, 0, wx.EXPAND, 0)
        grid_sizer_customize.Add(self.label_line_color, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_customize.Add(self.button_line_color, 0, wx.EXPAND, 0)
        grid_sizer_customize.Add(self.label_line_style, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_customize.Add(self.combo_line_style, 0, wx.EXPAND, 0)
        grid_sizer_customize.Add(self.label_line_width, 0, wx.ALIGN_CENTER_VERTICAL, 0)
        grid_sizer_customize.Add(self.spin_line_width, 0, wx.EXPAND, 0)
        grid_sizer_customize.AddGrowableCol(1)
        sizer_customize.Add(grid_sizer_customize, 1, wx.ALL | wx.EXPAND, 5)
        sizer_subdiv.Add(sizer_customize, 0, wx.ALL | wx.EXPAND, 5)
        sizer_div.Add(sizer_subdiv, 1, wx.EXPAND, 0)
	separator.Add(sizer_div, 1, wx.EXPAND, 0)
	separator.Add(self.button_apply_style, 0, wx.RIGHT | wx.BOTTOM | wx.ALIGN_RIGHT, 5)
        self.SetSizer(separator)
        self.Layout()

    def __insert_style_modes(self):
        # Format for adding new graphics style modes:
	# self.list_modes.Append(<Mode name>, ModeStyle(<bg color>, <grid color>, <line color>, <num line style>, <num line width>))
	
	self.list_modes.Append(_("Default"), ModeStyle('#FFFFFF', '#666666', '#0000FF', 0, 1))
	self.list_modes.Append("Contrast", ModeStyle('#000000', '#FFFFFF', '#FFFFFF', 0, 1))
	self.list_modes.Append("Simple", ModeStyle('#FFFFFF', '#000000', '#000000', 0, 1))
	self.list_modes.Append("Hacker", ModeStyle('#000000', '#00FF00', '#00FF00', 0, 1))
	self.list_modes.Append("Aqua", ModeStyle('#0E2581', '#FFFFFF', '#00EDF6', 0, 1))
	self.list_modes.Append("Inferno", ModeStyle('#6B0000', '#FFFFFF', '#FF0000', 0, 1))
	self.list_modes.Append("Tropical", ModeStyle('#00B829', '#006207', '#FFFF00', 1, 2))
	self.list_modes.Append("Desert", ModeStyle('#A94000', '#FFC800', '#FFB612', 0, 1))
	self.list_modes.Append("Night", ModeStyle('#000000', '#FFFF00', '#FFFF00', 3, 2))

    def __get_name_line_style(self, n):
	name = None
	if n == 0: name = "solid"
	elif n == 1: name = "dashed"
	elif n == 2: name = "dashdot"
	elif n == 3: name = "dotted"
	return name


    def on_select_mode(self, event):
	style_conf = self.list_modes.GetClientData(self.list_modes.GetSelection())
	self.button_bg_color.SetColour(style_conf.bg_color)
	self.button_grid_color.SetColour(style_conf.grid_color)
	self.button_line_color.SetColour(style_conf.line_color)
	self.combo_line_style.SetSelection(style_conf.line_style)
	self.spin_line_width.SetValue(style_conf.line_width)

	self.axes.set_axis_bgcolor(style_conf.bg_color)
	self.axes.grid(True, color=style_conf.grid_color)
	self.plot_data.set_color(style_conf.line_color)
	self.plot_data.set_linestyle(self.__get_name_line_style(style_conf.line_style))
	self.plot_data.set_linewidth(style_conf.line_width)

	self.canvas.draw()
	
    def on_change_bg_color(self, event):
	new_bg_color = GetColorString(self.button_bg_color.GetColour()) 
	self.axes.set_axis_bgcolor(new_bg_color)
	self.canvas.draw()
	self.list_modes.SetSelection(wx.NOT_FOUND)

    def on_change_grid_color(self, event):
	new_grid_color = GetColorString(self.button_grid_color.GetColour()) 
	self.axes.grid(True, color=new_grid_color)
	self.canvas.draw()
	self.list_modes.SetSelection(wx.NOT_FOUND)

    def on_change_line_color(self, event):
	new_line_color = GetColorString(self.button_line_color.GetColour()) 
	self.plot_data.set_color(new_line_color)
	self.canvas.draw()
	self.list_modes.SetSelection(wx.NOT_FOUND)

    def on_change_line_style(self, event):
	new_line_style = self.combo_line_style.GetSelection()
	self.plot_data.set_linestyle(self.__get_name_line_style(new_line_style))
	self.canvas.draw()
	self.list_modes.SetSelection(wx.NOT_FOUND)

    def on_change_line_width(self, event):
	new_line_width = self.spin_line_width.GetValue()
	self.plot_data.set_linewidth(new_line_width)
	self.canvas.draw()
	self.list_modes.SetSelection(wx.NOT_FOUND)

    def on_apply_style(self, event):
	self.EndModal(0)

    def GetModeName(self):
	name_mode = None
	if self.list_modes.GetSelection() == wx.NOT_FOUND:
		name_mode = _("Customized")
	else:
		name_mode = self.list_modes.GetString(self.list_modes.GetSelection())
	return name_mode

    def GetBgColor(self):
	return GetColorString(self.button_bg_color.GetColour()) 

    def GetGridColor(self):
	return GetColorString(self.button_grid_color.GetColour()) 

    def GetLineColor(self):
	return GetColorString(self.button_line_color.GetColour()) 

    def GetLineStyle(self):
	return self.__get_name_line_style(self.combo_line_style.GetSelection())

    def GetLineWidth(self):
	return self.spin_line_width.GetValue()

class ModeStyle(object):

	def __init__(self, bg_color, grid_color, line_color, line_style, line_width):
		self.bg_color = bg_color
		self.grid_color = grid_color
		self.line_color = line_color
		self.line_style = line_style
		self.line_width = line_width
