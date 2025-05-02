# Firefly
A research project conducted at Robotics Lab at Lund University.

Upstream: https://lgit945.cs.lth.se/robotlab/firefly


The goal was to creating a service and subscriber for force and torque samples from a sensor with low level hard real time requirements using C, FreeRTOS, Linux and Xenomai. We designed and implemented a general system and API for connecting nodes speaking a sample language and let them communicate over logical channels with different transport methods e.g. (real time) Ethernet, UDP, TCP or sneakernet.

**Tech stack:** Embedded C, Shell scripting, C++, GNU Linux, Robot Operating System, FreeRTOS, lwIP, CUnit, GDB, GNU
Make, Clang, LATEX, Git, SVN, Doxygen


## What is Firefly?
<img src="doc/concept-slides.png" alt="Concept slide" width="50%">

Legend:<br>
<img src="doc/concept-slides-legend.png" alt="Concept slide legend" width="30%">


Firefly is a framework and protocol that is used to communicate serialized data
between two nodes. It builds upon the Labcomm protocol, which is by design a one
way protocol, and extends it to enable two way communication over different
transport medium.

The protocol specification is fairly simple by design to avoid any overhead when
used in a setting with real time demands. There is a BNF-description of the
protocol.

There are reference implementations for three different application examples as
well as four different transport mediums (UDP and Ethernet on Posix and
FreeRTOS/Stellaris).

## WARNING
The library's interface is still not set in stone and very prone to changes. Do
_NOT_ consider this as stable yet!

