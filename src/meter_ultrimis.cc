/*
 Copyright (C) 2017-2020 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"dvparser.h"
#include"meters.h"
#include"meters_common_implementation.h"
#include"wmbus.h"
#include"wmbus_utils.h"
#include"util.h"

using namespace std;

struct MeterUltrimis : public virtual MeterCommonImplementation {
    MeterUltrimis(MeterInfo &mi);

    // Total water counted through the meter
    double totalWaterConsumption(Unit u);
    double targetWaterConsumption(Unit u);
    double totalBackwardFlow(Unit u);
    bool  hasTotalWaterConsumption();

private:
    void processContent(Telegram *t);

    uint32_t info_codes_ {}; // Really only 24 bits.
    double total_water_consumption_m3_ {};
    double target_water_consumption_m3_ {};
    double total_backward_flow_m3_ {};

    string status();
};

shared_ptr<Meter> createUltrimis(MeterInfo &mi)
{
    return shared_ptr<Meter>(new MeterUltrimis(mi));
}

MeterUltrimis::MeterUltrimis(MeterInfo &mi) :
    MeterCommonImplementation(mi, "ultrimis")
{
    setMeterType(MeterType::WaterMeter);

    setExpectedTPLSecurityMode(TPLSecurityMode::AES_CBC_IV);

    addLinkMode(LinkMode::T1);

    addPrint("total", Quantity::Volume,
             [&](Unit u){ return totalWaterConsumption(u); },
             "The total water consumption recorded by this meter.",
             PrintProperty::FIELD | PrintProperty::JSON);

    addPrint("target", Quantity::Volume,
             [&](Unit u){ return targetWaterConsumption(u); },
             "The total water consumption recorded at the beginning of this month.",
             PrintProperty::FIELD | PrintProperty::JSON);

    addPrint("current_status", Quantity::Text,
             [&](){ return status(); },
             "Status of meter.",
             PrintProperty::FIELD | PrintProperty::JSON);

    addPrint("total_backward_flow", Quantity::Volume,
             [&](Unit u){ return totalBackwardFlow(u); },
             "The total water backward flow.",
             PrintProperty::FIELD | PrintProperty::JSON);

}

string MeterUltrimis::status()
{
    /* According to the manual this meter offers these alarms:
       Back flow
       Meter leak
       Water main leak
       Zero flow
       Tampering detected
       No water
       Low battery
    */
    string info = "OK";
    if (info_codes_ != 0)
    {
        info = tostrprintf("ERR(%06x)", info_codes_);
    }
    return info;
}

void MeterUltrimis::processContent(Telegram *t)
{
    /*
      (ultrimis) 11: 04 dif (32 Bit Integer/Binary Instantaneous value)
      (ultrimis) 12: 13 vif (Volume l)
      (ultrimis) 13: * 320C0000 total consumption (3.122000 m3)
      (ultrimis) 17: 03 dif (24 Bit Integer/Binary Instantaneous value)
      (ultrimis) 18: FD vif (Second extension of VIF-codes)
      (ultrimis) 19: 17 vife (Error flags (binary))
      (ultrimis) 1a: 0C0C0C
      (ultrimis) 1d: 44 dif (32 Bit Integer/Binary Instantaneous value storagenr=1)
      (ultrimis) 1e: 13 vif (Volume l)
      (ultrimis) 1f: 21090000
      (ultrimis) 23: 04 dif (32 Bit Integer/Binary Instantaneous value)
      (ultrimis) 24: 93 vif (Volume l)
      (ultrimis) 25: 3C vife (backward flow)
      (ultrimis) 26: 05000000
    */
    int offset;
    string key;

    if(findKey(MeasurementType::Instantaneous, VIFRange::Volume, 0, 0, &key, &t->dv_entries)) {
        extractDVdouble(&t->dv_entries, key, &offset, &total_water_consumption_m3_);
        t->addMoreExplanation(offset, " total consumption (%f m3)", total_water_consumption_m3_);
    }

    extractDVuint24(&t->dv_entries, "03FD17", &offset, &info_codes_);
    t->addMoreExplanation(offset, " info codes (%s)", status().c_str());

    if(findKey(MeasurementType::Instantaneous, VIFRange::Volume, 1, 0, &key, &t->dv_entries)) {
        extractDVdouble(&t->dv_entries, key, &offset, &target_water_consumption_m3_);
        t->addMoreExplanation(offset, " target consumption (%f m3)", target_water_consumption_m3_);
    }

    extractDVdouble(&t->dv_entries, "04933C", &offset, &total_backward_flow_m3_);
    t->addMoreExplanation(offset, " total backward flow (%f m3)", total_backward_flow_m3_);
}

double MeterUltrimis::totalWaterConsumption(Unit u)
{
    assertQuantity(u, Quantity::Volume);
    return convert(total_water_consumption_m3_, Unit::M3, u);
}

bool MeterUltrimis::hasTotalWaterConsumption()
{
    return true;
}

double MeterUltrimis::targetWaterConsumption(Unit u)
{
    assertQuantity(u, Quantity::Volume);
    return convert(target_water_consumption_m3_, Unit::M3, u);
}

double MeterUltrimis::totalBackwardFlow(Unit u)
{
    assertQuantity(u, Quantity::Volume);
    return convert(total_backward_flow_m3_, Unit::M3, u);
}
