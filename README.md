# {PROJECT SONOFF}

## First Version:

This is the first model of the ESP-01 project firmware build!<br>
<p align="center">
  <a href="Relay1.ino">
    <img src="https://img.shields.io/badge/Relay1-blue" alt="Relay1">
  </a>
</p>
<p align="center">
  <a href="Relay2.ino">
    <img src="https://img.shields.io/badge/Relay2-blue" alt="Relay2">
  </a>
</p><br>
This is a way to make two relays work as twins using their own MAC Address, without the help of a router and external Wi-Fi.<br>
In ths project, we use relays with a module Wi-Fi called ESP-01 of the ESP8266 line. The idea is to make the relay be able to communicate with another, to do this the relay must be both an Access Point as well as a Client. Even though we are using only two in this starter, it is possible to use multiple relays that can ask the status of each relay and alter their state depending on your wishes, it can be a toggle function as well as a synchronized function.<br>

For this, I used the protocol ESP-NOW, that uses MAC address to exchange information, without the need of using wifi in the case of a power outage and the router can turn off making it unable to use the relays.
