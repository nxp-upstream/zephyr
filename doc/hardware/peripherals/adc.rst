.. _adc_api:

Analog-to-Digital Converter (ADC)
#################################

Overview
********

The ADC device class provides an API to access ADC devices.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_ADC`
* :kconfig:option:`CONFIG_ADC_SHELL`
* :kconfig:option:`CONFIG_ADC_ASYNC`

API Reference
*************

.. doxygengroup:: adc_interface

DT Spec APIs
============

These APIs are useful for specifying ADC channel information in Devicetree.

.. doxygengroup:: adc_dt_api
