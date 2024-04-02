cmd_/home/jhentges/dev/apci/modules.order := {   echo /home/jhentges/dev/apci/apci.ko; :; } | awk '!x[$$0]++' - > /home/jhentges/dev/apci/modules.order
