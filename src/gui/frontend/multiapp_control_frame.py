# -*- coding: UTF-8 -*-

#
# multiapp_control_frame.py
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
from backend.user_config import *
from frontend.final_config_panel import *
from frontend.monitoring_frame import *

class MultiAppControlFrame(wx.Frame):
	def __init__(self, *args, **kwargs):
                kwds = {"style": wx.DEFAULT_FRAME_STYLE}
                wx.Frame.__init__(self, *args, **kwds)

                self.version = kwargs.get("version")
                self.final_panel = kwargs.get("final_panel")
                self.user_config = kwargs.get("user_config")
                
                self.nr_total_apps = len(self.final_panel.pmc_extract.data)
                self.nr_complete_apps = 0
		self.list_apps = wx.ListBox(self, wx.ID_ANY)
		self.sizer_list_apps_staticbox = wx.StaticBox(self, wx.ID_ANY, _("List of applications to monitor"))
		self.progress_bar = wx.Gauge(self, range=self.nr_total_apps)
		self.label_status = wx.StaticText(self, wx.ID_ANY, _("{0}/{1} applications completed.").format(0, self.nr_total_apps), style=wx.ALIGN_RIGHT)
                self.timer_update_data = wx.Timer(self)
                self.app_running = None

                for app_name in self.final_panel.pmc_extract.data:
                    self.list_apps.Append(app_name + " " +  _("(waiting)"), app_name)

		self.__set_properties()
		self.__do_layout()

                self.Bind(wx.EVT_TIMER, self.on_timer_update_data, self.timer_update_data)
                self.Bind(wx.EVT_LISTBOX_DCLICK, self.on_dclick_app, self.list_apps)
                self.Bind(wx.EVT_CLOSE, self.on_close_frame)

	def __set_properties(self):
		self.SetTitle("PMCTrack-GUI v" + self.version + " - " + _("Monitoring applications"))
		self.SetSize((550, 350))
		self.list_apps.SetSelection(0)
                self.timer_update_data.Start(100)

	def __do_layout(self):
		separator = wx.BoxSizer(wx.VERTICAL)
		sizer_status = wx.BoxSizer(wx.HORIZONTAL)
		self.sizer_list_apps_staticbox.Lower()
		sizer_list_apps = wx.StaticBoxSizer(self.sizer_list_apps_staticbox, wx.HORIZONTAL)
		sizer_list_apps.Add(self.list_apps, 1, wx.ALL | wx.EXPAND, 5)
		separator.Add(sizer_list_apps, 1, wx.ALL | wx.EXPAND, 5)
		sizer_status.Add(self.progress_bar, 1, wx.ALL, 5)
                sizer_status.Add(self.label_status, 0, wx.ALL | wx.ALIGN_CENTER_VERTICAL, 5)
		separator.Add(sizer_status, 0, wx.EXPAND, 0)
		self.SetSizer(separator)
		self.Layout()

        def on_timer_update_data(self, event):
            if self.final_panel.pmc_extract.error != None:
                error_msg = _("PMCTrack error") + ":\n" + self.final_panel.pmc_extract.error
                dlg = wx.MessageDialog(parent=None, message=error_msg, caption=_("Error"), style=wx.OK | wx.ICON_ERROR)
                if dlg.ShowModal() == wx.ID_OK:
                    self.final_panel.StopMonitoring(False)
            elif self.app_running != self.final_panel.pmc_extract.app_running:
                if self.app_running != None and self.final_panel.pmc_extract.state[self.app_running] == "F":
                    self.list_apps.SetString(self.nr_complete_apps, self.app_running + " " +  _("(finished)"))
                    self.nr_complete_apps += 1

                self.app_running = self.final_panel.pmc_extract.app_running

                if self.app_running != None:
                    self.list_apps.SetString(self.nr_complete_apps, self.app_running + " " +  _("(running)"))
                
                self.progress_bar.SetValue(self.nr_complete_apps)
		self.label_status.SetLabel(_("{0}/{1} applications completed.").format(self.nr_complete_apps, self.nr_total_apps))

                if self.nr_complete_apps == self.nr_total_apps:
                    self.timer_update_data.Stop()
                    self.final_panel.button_monitoring.SetLabel(_("Close monitoring windows"))
                    dlg = wx.MessageDialog(parent=None, message=_("All applications are done."), 
                        caption=_("Information"), style=wx.OK|wx.ICON_INFORMATION)
                    dlg.ShowModal()
                    dlg.Destroy()

        def on_dclick_app(self, event):
                app = self.list_apps.GetClientData(self.list_apps.GetSelection())
                mon_frame = MonitoringFrame(None, -1, "", app_name=app, version=self.version, final_panel=self.final_panel, user_config=self.user_config)
                mon_frame.Show()

        def on_close_frame(self, event):
                self.final_panel.StopMonitoring()
