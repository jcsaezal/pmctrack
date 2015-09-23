#!/bin/sh

#
# generate_xml.sh
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

csv_path='/usr/share/pmctrack/events/'
xml_path='xml_definitions/'

if [ ! -d xml_path ]; then
  mkdir -p xml_path
fi

for f in `ls $csv_path`; do
 model="${f%.csv}"
 echo "Generating xml for $model..."
 # Generating xml through pmc-events
 pmc-events -x -m $model > ${xml_path}/${model}.xml
 # Adding dtd checking line to xml
 sed -i '2i<!DOCTYPE pmcs_and_events SYSTEM "events.dtd">' ${xml_path}/${model}.xml
 # Validating xml through xmliint
 echo -n "Validating ${model}.xml..."
 xmllint --noout --valid ${xml_path}/${model}.xml && echo 'OK'
done
