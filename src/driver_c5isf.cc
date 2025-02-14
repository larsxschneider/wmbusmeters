/*
 Copyright (C) 2022 Fredrik Öhrström (gpl-3.0-or-later)
               2022 Alexander Streit (gpl-3.0-or-later)

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

#include"meters_common_implementation.h"

struct MeterC5isf : public virtual MeterCommonImplementation
{
    MeterC5isf(MeterInfo &mi, DriverInfo &di);

    private:

    // Three types of telegrams (T1A1 T1A2 T1B) they all share total_energy_kwh and total_volume_m3.
    double total_energy_kwh_ {};
    double total_volume_m3_ {};

    // The T1A1 and T1B also contains a status.
    string status_;

    // T1A1 and T1A2 also contains the previous month dates.
    // We assume that they are identical for both types of telegrams
    // so we use the same storage here.
    string prev_month_date_[14];

    // T1A1 contains 14 back months of energy consumption.
    double total_energy_prev_month_kwh_[14] {};

    // T1A2 contains 14 back months of volume consumption.
    double total_volume_prev_month_m3_[14] {};

    // T1B contains:
    double due_energy_kwh_ {};
    string due_date_;
    double volume_flow_m3h_ {};
    double power_kw_ {};
    double total_energy_last_month_kwh_ {};
    string last_month_date_;
    double max_power_last_month_kw_ {};
    double flow_temperature_c_ {};
    double return_temperature_c_ {};
};

static bool ok = registerDriver([](DriverInfo&di)
{
    di.setName("c5isf");
    di.setMeterType(MeterType::HeatMeter);
    di.setExpectedTPLSecurityMode(TPLSecurityMode::AES_CBC_IV);
    di.addLinkMode(LinkMode::T1);
    di.addDetection(MANUFACTURER_ZRI, 0x0d, 0x88); // Telegram type T1A1
    di.addDetection(MANUFACTURER_ZRI, 0x07, 0x88); // Telegram type T1A2
    di.addDetection(MANUFACTURER_ZRI, 0x04, 0x88); // Telegram type T1B
    di.setConstructor([](MeterInfo& mi, DriverInfo& di){ return shared_ptr<Meter>(new MeterC5isf(mi, di)); });
});

MeterC5isf::MeterC5isf(MeterInfo &mi, DriverInfo &di) : MeterCommonImplementation(mi, di)
{
    // Fields common for T1A1, T1A2, T1B...........

    addNumericFieldWithExtractor(
        "total_energy_consumption",
        Quantity::Energy,
        FIND_FIELD(MeasurementType::Instantaneous, VIFRange::EnergyWh),
        PrintProperty::JSON | PrintProperty::FIELD | PrintProperty::IMPORTANT,
        "The total heat energy consumption recorded by this meter.",
        SET_FUNC(total_energy_kwh_, Unit::KWH),
        GET_FUNC(total_energy_kwh_, Unit::KWH));

    addNumericFieldWithExtractor(
        "total_volume",
        Quantity::Volume,
        FIND_FIELD(MeasurementType::Instantaneous, VIFRange::Volume),
        PrintProperty::JSON | PrintProperty::FIELD | PrintProperty::IMPORTANT,
        "The total heating media volume recorded by this meter.",
        SET_FUNC(total_volume_m3_, Unit::M3),
        GET_FUNC(total_volume_m3_, Unit::M3));

    // Status field common for T1A1 and T1B

    addStringFieldWithExtractorAndLookup(
        "status",
        Quantity::Text,
        DifVifKey("02FD17"),
        MeasurementType::Instantaneous,
        VIFRange::Any,
        AnyStorageNr,
        AnyTariffNr,
        IndexNr(1),
        PrintProperty::JSON | PrintProperty::FIELD | PrintProperty::IMPORTANT,
        "Status and error flags.",
        SET_STRING_FUNC(status_),
        GET_STRING_FUNC(status_),
         {
            {
                {
                    "ERROR_FLAGS",
                    Translate::Type::DecimalsToString,
                    9999,
                    "OK",
                    {
                        { 2000, "VERIFICATION_EXPIRED" }, // Status initial verification expired
                        { 1000, "BATTERY_EXPIRED" }, // END Status end of the battery
                        { 800, "WIRELESS_ERROR" }, // Wireless interface
                        { 100, "HARDWARE_ERROR3" }, // Hardware error
                        { 50, "VALUE_OVERLOAD" }, // Measured value outside overload range
                        { 40, "AIR_INSIDE" }, // Air inside the medium Vent system (**)
                        { 30, "REVERSE_FLOW" }, // Reverse water flow detected
                        { 20, "DRY" }, // No water in the measuring tube
                        { 10, "ERROR_MEASURING" }, // Error in the measuring system
                        { 9, "HARDWARE_ERROR2" }, // Hardware error Exchange device
                        { 8, "HARDWARE_ERROR1" }, //  Hardware error Exchange device
                        { 7, "LOW_BATTERY" }, // Battery voltage Exchange device
                        { 6, "SUPPLY_SENSOR_INTERRUPTED" }, // Interruption supply sensor Check sensors
                        { 5, "SHORT_CIRCUIT_SUPPLY_SENSOR" }, // Short circuit supply sensor Check sensors
                        { 4, "RETURN_SENSOR_INTERRUPTED" }, // Interruption return sensor
                        { 3, "SHORT_CIRCUIT_RETURN_SENSOR" }, // Short circuit return sensor / Check sensors
                        { 2, "TEMP_ABOVE_RANGE" }, // Temperature above of measuring range
                        { 1, "TEMP_BELOW_RANGE" }, // Temperature below of measuring range
                    }
                },
            },
         });

    // Dates are common to T1A1 and T1A2 ///////////////////////////////////////////////////////

    for (int i=0; i<14; ++i)
    {
        addStringFieldWithExtractor(
            tostrprintf("prev_%d_month", i+1),
            Quantity::Text,
            FIND_SFIELD_S(MeasurementType::Instantaneous, VIFRange::Date, StorageNr(32+i)),
            PrintProperty::JSON,
            "The due date.",
            SET_STRING_FUNC(prev_month_date_[i]),
            GET_STRING_FUNC(prev_month_date_[i]));
    }

    // Telegram type T1A1 ///////////////////////////////////////////////////////

    for (int i=0; i<14; ++i)
    {
        addNumericFieldWithExtractor(
            tostrprintf("prev_%d_month", i+1),
            Quantity::Energy,
            FIND_FIELD_S(MeasurementType::Instantaneous, VIFRange::EnergyWh, StorageNr(32+i)),
            PrintProperty::JSON,
            "The total heat energy consumption recorded at end of previous month.",
            SET_FUNC(total_energy_prev_month_kwh_[i], Unit::KWH),
            GET_FUNC(total_energy_prev_month_kwh_[i], Unit::KWH));
    }

    // Telegram type T1A2 ///////////////////////////////////////////////////////

    for (int i=0; i<14; ++i)
    {
        addNumericFieldWithExtractor(
            tostrprintf("prev_%d_month", i+1),
            Quantity::Volume,
            FIND_FIELD_S(MeasurementType::Instantaneous, VIFRange::Volume, StorageNr(32+i)),
            PrintProperty::JSON,
            tostrprintf("Previous month %d last date.", i+1),
            SET_FUNC(total_volume_prev_month_m3_[i], Unit::M3),
            GET_FUNC(total_volume_prev_month_m3_[i], Unit::M3));
    }

    // Telegram type T1B ///////////////////////////////////////////////////////

    addNumericFieldWithExtractor(
        "due_energy_consumption",
        Quantity::Energy,
        FIND_FIELD_S(MeasurementType::Instantaneous, VIFRange::EnergyWh, StorageNr(8)),
        PrintProperty::JSON,
        "The total heat energy consumption at the due date.",
        SET_FUNC(due_energy_kwh_, Unit::KWH),
        GET_FUNC(due_energy_kwh_, Unit::KWH));

    addStringFieldWithExtractor(
        "due_date",
        Quantity::Text,
        FIND_SFIELD_S(MeasurementType::Instantaneous, VIFRange::Date, StorageNr(8)),
        PrintProperty::JSON,
        "The due date.",
        SET_STRING_FUNC(due_date_),
        GET_STRING_FUNC(due_date_));

    addNumericFieldWithExtractor(
        "volume_flow",
        Quantity::Flow,
        FIND_FIELD(MeasurementType::Instantaneous, VIFRange::VolumeFlow),
        PrintProperty::JSON,
        "The current heat media volume flow.",
        SET_FUNC(volume_flow_m3h_, Unit::M3H),
        GET_FUNC(volume_flow_m3h_, Unit::M3H));

    addNumericFieldWithExtractor(
        "power",
        Quantity::Power,
        FIND_FIELD(MeasurementType::Instantaneous, VIFRange::PowerW),
        PrintProperty::JSON,
        "The current power consumption.",
        SET_FUNC(power_kw_, Unit::KW),
        GET_FUNC(power_kw_, Unit::KW));

    addNumericFieldWithExtractor(
        "total_energy_consumption_last_month",
        Quantity::Energy,
        FIND_FIELD_S(MeasurementType::Instantaneous, VIFRange::EnergyWh, StorageNr(32)),
        PrintProperty::JSON,
        "The total heat energy consumption recorded at end of last month.",
        SET_FUNC(total_energy_last_month_kwh_, Unit::KWH),
        GET_FUNC(total_energy_last_month_kwh_, Unit::KWH));

    addStringFieldWithExtractor(
        "last_month_date",
        Quantity::Text,
        FIND_SFIELD_S(MeasurementType::Instantaneous, VIFRange::Date, StorageNr(32)),
        PrintProperty::JSON,
        "The due date.",
        SET_STRING_FUNC(last_month_date_),
        GET_STRING_FUNC(last_month_date_));

    addNumericFieldWithExtractor(
        "max_power_last_month",
        Quantity::Power,
        NoDifVifKey,
        VifScaling::Auto,
        MeasurementType::Maximum,
        VIFRange::PowerW,
        StorageNr(32),
        TariffNr(0),
        IndexNr(1),
        PrintProperty::JSON,
        "Maximum power consumption last month.",
        SET_FUNC(max_power_last_month_kw_, Unit::KW),
        GET_FUNC(max_power_last_month_kw_, Unit::KW));

    addNumericFieldWithExtractor(
        "flow_temperature",
        Quantity::Temperature,
        NoDifVifKey,
        VifScaling::Auto,
        MeasurementType::Instantaneous,
        VIFRange::FlowTemperature,
        StorageNr(0),
        TariffNr(0),
        IndexNr(1),
        PrintProperty::JSON,
        "The current forward heat media temperature.",
        SET_FUNC(flow_temperature_c_, Unit::C),
        GET_FUNC(flow_temperature_c_, Unit::C));

    addNumericFieldWithExtractor(
        "return_temperature",
        Quantity::Temperature,
        NoDifVifKey,
        VifScaling::Auto,
        MeasurementType::Instantaneous,
        VIFRange::ReturnTemperature,
        StorageNr(0),
        TariffNr(0),
        IndexNr(1),
        PrintProperty::JSON,
        "The current return heat media temperature.",
        SET_FUNC(return_temperature_c_, Unit::C),
        GET_FUNC(return_temperature_c_, Unit::C));


}

// Test: Heat c5isf 55445555 NOKEY

// telegram=|E544496A55554455880D7A320200002F2F_04060000000004130000000002FD17240084800106000000008280016C2124C480010600000080C280016CFFFF84810106000000808281016CFFFFC481010600000080C281016CFFFF84820106000000808282016CFFFFC482010600000080C282016CFFFF84830106000000808283016CFFFFC483010600000080C283016CFFFF84840106000000808284016CFFFFC484010600000080C284016CFFFF84850106000000808285016CFFFFC485010600000080C285016CFFFF84860106000000808286016CFFFFC486010600000080C286016CFFFF|
// {"media":"heat/cooling load","meter":"c5isf","name":"Heat","id":"55445555","total_energy_consumption_kwh":0,"total_volume_m3":0,"status":"REVERSE_FLOW SUPPLY_SENSOR_INTERRUPTED","prev_1_month":"2017-04-01","prev_2_month":"2127-15-31","prev_3_month":"2127-15-31","prev_4_month":"2127-15-31","prev_5_month":"2127-15-31","prev_6_month":"2127-15-31","prev_7_month":"2127-15-31","prev_8_month":"2127-15-31","prev_9_month":"2127-15-31","prev_10_month":"2127-15-31","prev_11_month":"2127-15-31","prev_12_month":"2127-15-31","prev_13_month":"2127-15-31","prev_14_month":"2127-15-31","prev_1_month_kwh":0,"prev_2_month_kwh":2147483648,"prev_3_month_kwh":2147483648,"prev_4_month_kwh":2147483648,"prev_5_month_kwh":2147483648,"prev_6_month_kwh":2147483648,"prev_7_month_kwh":2147483648,"prev_8_month_kwh":2147483648,"prev_9_month_kwh":2147483648,"prev_10_month_kwh":2147483648,"prev_11_month_kwh":2147483648,"prev_12_month_kwh":2147483648,"prev_13_month_kwh":2147483648,"prev_14_month_kwh":2147483648,"prev_1_month_m3":0,"prev_2_month_m3":0,"prev_3_month_m3":0,"prev_4_month_m3":0,"prev_5_month_m3":0,"prev_6_month_m3":0,"prev_7_month_m3":0,"prev_8_month_m3":0,"prev_9_month_m3":0,"prev_10_month_m3":0,"prev_11_month_m3":0,"prev_12_month_m3":0,"prev_13_month_m3":0,"prev_14_month_m3":0,"due_energy_consumption_kwh":0,"due_date":"","volume_flow_m3h":0,"power_kw":0,"total_energy_consumption_last_month_kwh":0,"last_month_date":"2017-04-01","max_power_last_month_kw":0,"flow_temperature_c":0,"return_temperature_c":0,"timestamp":"1111-11-11T11:11:11Z"}
// |Heat;55445555;0.000000;0.000000;REVERSE_FLOW SUPPLY_SENSOR_INTERRUPTED;1111-11-11 11:11.11

// Type T1A2 telegram:
// telegram=|DA44496A5555445588077A320200002F2F_04140000000084800114000000008280016C2124C480011400000080C280016CFFFF84810114000000808281016CFFFFC481011400000080C281016CFFFF84820114000000808282016CFFFFC482011400000080C282016CFFFF84830114000000808283016CFFFFC483011400000080C283016CFFFF84840114000000808284016CFFFFC484011400000080C284016CFFFF84850114000000808285016CFFFFC485011400000080C285016CFFFF84860114000000808286016CFFFFC486011400000080C286016CFFFF|
// {"media":"water","meter":"c5isf","name":"Heat","id":"55445555","total_energy_consumption_kwh":0,"total_volume_m3":0,"status":"REVERSE_FLOW SUPPLY_SENSOR_INTERRUPTED","prev_1_month":"2017-04-01","prev_2_month":"2127-15-31","prev_3_month":"2127-15-31","prev_4_month":"2127-15-31","prev_5_month":"2127-15-31","prev_6_month":"2127-15-31","prev_7_month":"2127-15-31","prev_8_month":"2127-15-31","prev_9_month":"2127-15-31","prev_10_month":"2127-15-31","prev_11_month":"2127-15-31","prev_12_month":"2127-15-31","prev_13_month":"2127-15-31","prev_14_month":"2127-15-31","prev_1_month_kwh":0,"prev_2_month_kwh":2147483648,"prev_3_month_kwh":2147483648,"prev_4_month_kwh":2147483648,"prev_5_month_kwh":2147483648,"prev_6_month_kwh":2147483648,"prev_7_month_kwh":2147483648,"prev_8_month_kwh":2147483648,"prev_9_month_kwh":2147483648,"prev_10_month_kwh":2147483648,"prev_11_month_kwh":2147483648,"prev_12_month_kwh":2147483648,"prev_13_month_kwh":2147483648,"prev_14_month_kwh":2147483648,"prev_1_month_m3":0,"prev_2_month_m3":21474836.48,"prev_3_month_m3":21474836.48,"prev_4_month_m3":21474836.48,"prev_5_month_m3":21474836.48,"prev_6_month_m3":21474836.48,"prev_7_month_m3":21474836.48,"prev_8_month_m3":21474836.48,"prev_9_month_m3":21474836.48,"prev_10_month_m3":21474836.48,"prev_11_month_m3":21474836.48,"prev_12_month_m3":21474836.48,"prev_13_month_m3":21474836.48,"prev_14_month_m3":21474836.48,"due_energy_consumption_kwh":0,"due_date":"","volume_flow_m3h":0,"power_kw":0,"total_energy_consumption_last_month_kwh":0,"last_month_date":"2017-04-01","max_power_last_month_kw":0,"flow_temperature_c":0,"return_temperature_c":0,"timestamp":"1111-11-11T11:11:11Z"}
// |Heat;55445555;0.000000;0.000000;REVERSE_FLOW SUPPLY_SENSOR_INTERRUPTED;1111-11-11 11:11.11

// Type T1B telegram:
// telegram=|5E44496A5555445588047A0A0050052F2F_04061A0000000413C20800008404060000000082046CC121043BA4000000042D1900000002591216025DE21002FD17000084800106000000008280016CC121948001AE25000000002F2F2F2F2F2F|
// {"media":"heat","meter":"c5isf","name":"Heat","id":"55445555","total_energy_consumption_kwh":26,"total_volume_m3":2.242,"status":"OK","prev_1_month":"2022-01-01","prev_2_month":"2127-15-31","prev_3_month":"2127-15-31","prev_4_month":"2127-15-31","prev_5_month":"2127-15-31","prev_6_month":"2127-15-31","prev_7_month":"2127-15-31","prev_8_month":"2127-15-31","prev_9_month":"2127-15-31","prev_10_month":"2127-15-31","prev_11_month":"2127-15-31","prev_12_month":"2127-15-31","prev_13_month":"2127-15-31","prev_14_month":"2127-15-31","prev_1_month_kwh":0,"prev_2_month_kwh":2147483648,"prev_3_month_kwh":2147483648,"prev_4_month_kwh":2147483648,"prev_5_month_kwh":2147483648,"prev_6_month_kwh":2147483648,"prev_7_month_kwh":2147483648,"prev_8_month_kwh":2147483648,"prev_9_month_kwh":2147483648,"prev_10_month_kwh":2147483648,"prev_11_month_kwh":2147483648,"prev_12_month_kwh":2147483648,"prev_13_month_kwh":2147483648,"prev_14_month_kwh":2147483648,"prev_1_month_m3":0,"prev_2_month_m3":21474836.48,"prev_3_month_m3":21474836.48,"prev_4_month_m3":21474836.48,"prev_5_month_m3":21474836.48,"prev_6_month_m3":21474836.48,"prev_7_month_m3":21474836.48,"prev_8_month_m3":21474836.48,"prev_9_month_m3":21474836.48,"prev_10_month_m3":21474836.48,"prev_11_month_m3":21474836.48,"prev_12_month_m3":21474836.48,"prev_13_month_m3":21474836.48,"prev_14_month_m3":21474836.48,"due_energy_consumption_kwh":0,"due_date":"2022-01-01","volume_flow_m3h":0.164,"power_kw":2.5,"total_energy_consumption_last_month_kwh":0,"last_month_date":"2022-01-01","max_power_last_month_kw":0,"flow_temperature_c":56.5,"return_temperature_c":43.22,"timestamp":"1111-11-11T11:11:11Z"}
// |Heat;55445555;26.000000;2.242000;OK;1111-11-11 11:11.11
