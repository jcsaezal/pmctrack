 <p align="center">
  <span>Language:</span> 
  <a href="https://github.com/jcsaezal/pmctrack/spanish.md">Español</a> |
  <a href="https://github.com/jcsaezal/pmctrack">English</a> 
</p>

PMCTrack es una herramienta de monitoreo de rendimiento open-source(código abierto) orientada al sistema operativo GNU / Linux.
Esta herramienta de rendimiento ha sido diseñada específicamente para ayudar a los desarrolladores de kernel a implementar algoritmos 
de planificación en Linux que aprovechan los datos de los contadores de monitoreo de rendimiento (PMC) para realizar optimizaciones en el 
tiempo de ejecución. A diferencia de otras herramientas de monitoreo, las características de PMCTrack y la API en el kernel permiten que
el planificador(scheduler) del sistema operativo acceda a los datos de PMC por hilo de manera independiente de la arquitectura.
A pesar de ser una herramienta orientada al sistema operativo, PMCTrack también permite recopilar valores de PMC del espacio del usuario, lo 
que posibilita a los desarrolladores del núcleo realizar el análisis y la depuración necesarios para ayudarlos durante el proceso de diseño 
del planificador. Además, la herramienta proporciona tanto al planificador como a los componentes PMCTrack del espacio de usuario otras 
métricas intuitivas disponibles en 
procesadores modernos que no están expuestos directamente como PMC, la ocupación de caché o el consumo de energía.

[![Analytics](https://ga-beacon.appspot.com/UA-76163836-1/github-pmctrack/readme)](http://pmctrack.dacya.ucm.es)

## Colaboradores del Proyecto

* Juan Carlos Saez Alcaide (<jcsaezal@ucm.es>) - Creador de PMCTrack y principal maintainer. 
* Jorge Casas Hernan - Maintainer del PMCTrack-GUI
* Abel Serrano Juste (conocido como [@Akronix](https://github.com/Akronix))
* Javier Setoain

## Anteriores colaboradores 

* Guillermo Martinez Fernandez
* Sergio Sanchez Gordo
* Sofia Dronda Merino

## Publicaciones


* Juan Carlos Saez, Adrian Pousa, Roberto Rodríguez-Rodríguez, Fernando Castro, Manuel Prieto-Matias. (2017) "PMCTrack: Delivering performance monitoring counter 
support to the OS scheduler", *The Computer Journal*, Volume 60, Issue 1, 1 January 2017, pp. 60–85. ([pdf](https://artecs.dacya.ucm.es/files/cj-jcsaez-pmctrack-2017.pdf)) 
* Juan Carlos Saez, Jorge Casas, Abel Serrano, Roberto Rodríguez-Rodríguez, Fernando Castro, Daniel Chaver, Manuel Prieto-Matias. (2015)
"An OS-Oriented Performance Monitoring Tool for Multicore Systems," _Euro-Par 2015 International Workshops: Revised Selected Papers_, pp. 697-709. 

## Requisitos del Sistema

Para emplear PMCTrack, debe instalarse un kernel de Linux parcheado en la máquina. En el directorio `src / kernel-patches` se pueden
encontrar varios parches de kernel para varias versiones de Linux. El nombre de cada archivo de parche codifica la versión del kernel 
de Linux donde se debe aplicar el parche, así como la arquitectura del procesador compatible. El formato es el siguiente:

	pmctrack_linux-<version_del_kernel>_<arquitectura>.patch 
		
Para compilar el kernel para PMCTrack, la siguiente opción debe estar habilitada al configurar el kernel:

    CONFIG_PMCTRACK=y
		
Los headers del kernel para la versión parcheada de Linux también deben instalarse en el sistema. Esto es necesario para una compilación
exitosa fuera del árbol del módulo del kernel de PMCTrack. Puede encontrar un Makefile listo para usar fuera de los árboles en las 
fuentes de los diferentes tipos del módulo del kernel.

La mayoría de los componentes de nivel de usuario de PMCTrack están escritos en C y no dependen de ninguna biblioteca externa (más allá
de la libc, por supuesto). Se proporciona un Makefile separado para _libpmctrack_ así como para las diversas herramientas de línea de 
comandos. Como tal, debería ser sencillo construir estos componentes de software en la mayoría de las distribuciones de Linux.

También creamos PMCTrack-GUI, un front-end de Python para la herramienta de línea de comandos `pmctrack`. Esta aplicación amplía las 
capacidades de la pila PMCTrack con características como un modo de monitoreo remoto basado en SSH o la capacidad de trazar los valores 
de las métricas de rendimiento definidas por el usuario en tiempo real. Esta aplicación GUI se ejecuta en Linux y Mac OS X y tiene las 
siguientes dependencias de software:


* Python v2.7
* Matplotlib (Python library)
* sshpass (command)
* WxPython v3.0

En Debian y Ubuntu es necesario instalar el siguiente software:

	$ sudo apt-get install python2.7 python-matplotlib python-wxgtk3.0 sshpass 

En Mac OS X,PMCTrack-GUI ha sido testeado satisfactoriamente tras instalar las siguientes dependencias software usando MacPorts:

	## Instalar paquetes
	$ sudo port install py27-matplotlib py27-numpy py27-scipy py27-ipython py27-wxpython-3.0 sshpass
	
	## Configuración por defecto de matplotlib 
	$ mkdir  ~/.matplotlib
	$ cp /opt/local/Library/Frameworks/Python.framework/Versions/2.7/lib/python2.7/site-packages/matplotlib/mpl-data/matplotlibrc  ~/.matplotlib

	## Selecciona el interprete MacPorts Python27 por defecto
	$sudo port select --set python python27
	$sudo port select --set ipython ipython27

## Compilando PMCTrack con la fuente, para procesadores ARM y x86

La variable de entorno `PMCTRACK_ROOT` debe definirse para una ejecución exitosa de las diversas herramientas de línea de comandos de 
PMCTrack. La secuencia de comandos `shrc` que se encuentra en el directorio raíz del repositorio se puede usar para establecer la 
variable` PMCTRACK_ROOT` de manera apropiada, así como para agregar los directorios de las herramientas 
de la línea de comandos al PATH. Para hacer esto posible, ejecute el siguiente comando en el directorio raíz del repositorio:
```bash
  $ . shrc
```
Ahora componentes del nivel kernel y en el nivel usuario pueden compilarse de manera sencilla con el script `pmctrack-manager así:

	$ pmctrack-manager build
	**** System information ***
	Processor_vendor=Intel
	Kernel_HZ=250
	Processor_bitwidth=64
	***********************************************
	Press ENTER to start the build process...

	*************************************************
	*** Building supported PMCTrack kernel modules **
	*************************************************
	Building kernel module intel-core....
	============================================
	make -C /lib/modules/3.17.3.pmctrack-x86+/build M=/home/bench/pmctrack/src/modules/pmcs/intel-core modules
	make[1]: Entering directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_core.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mc_experiments.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/pmu_config_x86.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/cbuffer.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/monitoring_mod.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/syswide.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/intel_cmt_mm.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/intel_rapl_mm.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/ipc_sampling_sf_mm.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_intel_core.o
	  Building modules, stage 2.
	  MODPOST 1 modules
	  CC      /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_intel_core.mod.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_intel_core.ko
	make[1]: Leaving directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	Done!!
	============================================
	Building kernel module core2....
	============================================
	make -C /lib/modules/3.17.3.pmctrack-x86+/build M=/home/bench/pmctrack/src/modules/pmcs/core2 modules
	make[1]: Entering directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mc_experiments.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/pmu_config_x86.o
	/home/bench/pmctrack/src/modules/pmcs/core2/pmu_config_x86.c: In function 'init_pmu_props':
	/home/bench/pmctrack/src/modules/pmcs/core2/pmu_config_x86.c:155:6: warning: unused variable 'model_cpu' [-Wunused-variable]
	  int model_cpu=0;
	      ^
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/cbuffer.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/monitoring_mod.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/syswide.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/ipc_sampling_sf_mm.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core2.o
	  Building modules, stage 2.
	  MODPOST 1 modules
	  CC      /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core2.mod.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core2.ko
	make[1]: Leaving directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	Done!!
	============================================
	Building libpmctrack ....
	============================================
	make -C src all
	make[1]: Entering directory '/home/bench/pmctrack/src/lib/libpmctrack/src'
	cc -c -DHZ=250 -Wall -g -fpic -I ../include -I ../../../modules/pmcs/include/pmc -o core.o core.c
	cc -c -DHZ=250 -Wall -g -fpic -I ../include -I ../../../modules/pmcs/include/pmc -o pmu_info.o pmu_info.c
	cc -shared -DHZ=250 -o ../libpmctrack.so core.o pmu_info.o
	ar rcs ../libpmctrack.a core.o pmu_info.o 
	make[1]: Leaving directory '/home/bench/pmctrack/src/lib/libpmctrack/src'
	Done!!
	============================================
	Building pmc-events ....
	============================================
	cc  -DUSE_VFORK -Wall -g -I ../../modules/pmcs/include/pmc -I../../lib/libpmctrack/include   -c -o pmc-events.o pmc-events.c
	cc -o ../../../bin/pmc-events pmc-events.o  -L../../lib/libpmctrack -lpmctrack -static
	Done!!
	============================================
	Building pmctrack ....
	============================================
	cc  -DUSE_VFORK -Wall -g -I ../../modules/pmcs/include/pmc -I../../lib/libpmctrack/include   -c -o pmctrack.o pmctrack.c
	cc -o ../../../bin/pmctrack pmctrack.o  -L../../lib/libpmctrack -lpmctrack -static
	Done!!
	============================================
	*** BUILD PROCESS COMPLETED SUCCESSFULLY ***

El `pmctrack-manager` recupera información clave del sistema y crea las herramientas de línea de comandos, así como las 
diferentes versiones del módulo de kernel PMCTrack compatible con la plataforma actual. 
Si la compilación falla, los errores de compilación se pueden encontrar en el archivo `build.log` creado en el directorio actual.


## Compilando PMCTrack desde la fuente para Intel Xeon Phi

Para construir los diversos componentes de PMCTrack desde la fuente del coprocesador Xeon Phi
se debe usar el compilador cruzado `k1om-mpss-linux-gcc`. Este compilador se incluye con Intel MPSS. 
Para una compilación exitosa, `pmctrack-manager` debe conocer la ubicación de la instalación de Intel MPSS en el sistema de archivos. 
Además, la compilación fuera de árbol del módulo de kernel de PMCTrack para Xeon Phi requiere que la compilación se realice en un 
árbol de kernel de Linux recién construido (versión MPSS) con el parche de PMCTrack. El árbol de kernel de origen completo debe
encontrarse en el mismo sistema donde se realiza la compilación PMCTrack.

Una vez que se cumplen estos requisitos, se puede usar `pmctrack-manager` de la siguiente manera para realizar la compilación del 
Xeon Phi:


	$ pmctrack-manager build-phi <mpss-root-dir> <kernel-sources-dir>

## Usando PMCTrack desde el espacio del usuario

Una vez que se haya completado la compilación, vaya al directorio raíz de su copia local del repositorio de PMCTrack y configure 
las variables de entorno necesarias de la siguiente manera:

	$ . shrc

Después de eso, cargue cualquiera de las versiones disponibles del módulo de kernel de PMCTrack compatible con su procesador.
Tenga en cuenta que las compilaciones realizadas con `pmctrack-manager` (como se muestra arriba), solo compilan los flavors del módulo 
del kernel que pueden ser adecuados para su sistema.

La siguiente tabla resume las propiedades de los distintos flavors del módulo del kernel:


| Nombre | Ruta al archivo .ko | Procesadores soportados |  
| -----| ---------------------| ---------------------|  
| intel-core | `src/modules/pmcs/intel-core/mchw_intel_core.ko` | La mayoría de los procesadores multi-core de Intel son compatibles con este módulo, incluidos los procesadores recientes basados en la microarquitectura "Broadwell" de Intel. | 
| amd | `src/modules/pmcs/amd/mchw_amd.ko` | Este módulo ha sido probado con éxito en procesadores AMD Opteron. Sin embargo, debe ser compatible con todos los procesadores multinúcleo de AMD.| 
| arm | `src/modules/pmcs/arm/mchw_arm.ko` | Este módulo se ha probado con éxito en sistemas ARM con procesadores de 32 bits big.LITTLE, que combinan los núcleos ARM Cortex A7 con los núcleos ARM Cortex A15. Específicamente, las pruebas se realizaron en la ARM Coretile Express Development Board (TC2). | 
| odroid-xu | `src/modules/pmcs/odroid-xu/mchw_odroid_xu.ko` |  Módulo específico para placas Odroid XU3 y XU4. Se puede encontrar más información sobre estos boards en[www.hardkernel.com](http://www.hardkernel.com) | 
| arm64 | `src/modules/pmcs/arm64/mchw_arm64.ko` | Este módulo se ha probado con éxito en sistemas ARM con procesadores de 64 bits big.LITTLE, que combinan los núcleos ARM Cortex A57 con los núcleos ARM Cortex A53. Específicamente, se realizaron pruebas en el ARM Juno Development Board. | 
| xeon-phi | `src/modules/pmcs/xeon-phi/mchw_phi.ko` | Intel Xeon Phi Coprocessor | 
| core2 | `src/modules/pmcs/phi/mchw_core2.ko` | Este módulo ha sido diseñado específicamente para el sistema de prototipo Intel QuickIA. Intel QuickIA es un sistema de múltiples núcleos asimétricos de doble socket que cuenta con un procesador Intel Xeon E5450 de cuatro núcleos y un procesador dual-core Intel Atom N330. El módulo también funciona en procesadores Intel Atom y los "viejos" procesadores Intel multicore, como el Intel Core 2 Duo. Sin embargo, dados los numerosos hacks existentes para el QuickIA en este módulo, se recomienda a los usuarios usar el flavor más general "intel-core".  | 

Una vez que se haya identificado el modelo de kernel más adecuado para el sistema, el módulo se puede cargar en el kernel 
habilitado para PMCTrack en ejecución de la siguiente manera:

$ sudo insmod <path_to_the_ko_file>

Si el comando no devouelve errores, la información sobre las Unidades de Monitoreo de Rendimiento (PMU) detectadas en la máquina 
se puede recuperar leyendo del archivo `/ proc / pmc / info`:

	$ cat /proc/pmc/info 
	*** PMU Info ***
	nr_core_types=1
	[PMU coretype0]
	pmu_model=x86_intel-core.haswell-ep
	nr_gp_pmcs=8
	nr_ff_pmcs=3
	pmc_bitwidth=48
	***************
	*** Monitoring Module ***
	counter_used_mask=0x0
	nr_experiments=0
	nr_virtual_counters=0
	***************

 Alternativamente, el comando de ayuda `pmc-events` puede usarse para obtener la misma información en un formato ligeramente diferente:
 
 
	$ pmc-events -I
	[PMU 0]
	pmu_model=x86_intel-core.haswell-ep
	nr_fixed_pmcs=3
	nr_gp_pmcs=8

En los sistemas que cuentan con procesadores de múltiples núcleos asimétricos, como ARM big.LITTLE, el comando `pmc-event` mostrará una lista de tantas PMU ya que existen diferentes tipos de núcleos en el sistema. En un procesador ARM big.LITTLE de 64 bits, la salida será la siguiente:

	$ pmc-events -I
	[PMU 0]
	pmu_model=armv8.cortex_a53
	nr_fixed_pmcs=1
	nr_gp_pmcs=6
	[PMU 1]
	pmu_model=armv8.cortex_a57
	nr_fixed_pmcs=1
	nr_gp_pmcs=6
  
Para obtener una lista de los eventos de hardware compatibles, se puede usar el siguiente comando:

	$ pmc-events -L
	[PMU 0]
	instr_retired_fixed
	unhalted_core_cycles_fixed
	unhalted_ref_cycles_fixed
	instr
	cycles
	unhalted_core_cycles
	instr_retired
	unhalted_ref_cycles
	llc_references
	llc_references.prefetch
	llc_misses
	llc_misses.prefetch
	branch_instr_retired
	branch_mispred_retired
	l2_references
	l2_misses
	...


### La herramienta en línea de comandos `pmctrack` 
El comando `pmctrack` es la forma más directa de acceder a la funcionalidad de PMCTrack desde el espacio de usuario. 
Las opciones disponibles para el comando pueden enumerarse simplemente escribiendo `pmctrack` en la consola:


	$ pmctrack
	Usage: pmctrack [OPTION [OP. ARGS]] [PROG [ARGS]]
	Available options:
	        -c      <config-string>
	                set up a performance monitoring experiment using either raw or mnemonic-based PMC string
	        -o      <output>
	                output: set output file for the results. (default = stdout.)
	        -T      <Time>
	                Time: elapsed time in seconds between two consecutive counter samplings. (default = 1 sec.)
	        -b      <cpu or mask>
	                bind launched program to the specified cpu o cpumask.
	        -n      <max-samples>
	                Run command until a given number of samples are collected
	        -N      <secs>
	                Run command for secs seconds only
	        -e
	                Enable extended output
	        -A
	                Enable aggregate count mode
	        -k      <kernel_buffer_size>
	                Specify the size of the kernel buffer used for the PMC samples
	        -b      <cpu or mask>
	                bind monitor program to the specified cpu o cpumask.
	        -S
	                Enable system-wide monitoring mode (per-CPU)
	        -r
	                Accept pmc configuration strings in the RAW format
	        -p      <pmu>
	                Specify the PMU id to use for the event configuration
	        -L
	                Legacy-mode: do not show counter-to-event mapping
	        -t
	                Show real, user and sys time of child process
	PROG + ARGS:
	                Command line for the program to be monitored. 

Antes de introducir los conceptos básicos de este comando, vale la pena describir la semántica de las opciones `-c` y` -V`. 
Esencialmente, la opción -c acepta un argumento con una cadena que describe un conjunto de eventos de hardware a monitorear. 
Esta cadena consiste en configuraciones de eventos separados por comas; a su vez, una configuración de eventos se puede especificar
utilizando mnemotecnia de eventos o códigos hexadecimales de eventos que se encuentran en el manual de PMU provisto por el fabricante 
del procesador. Por ejemplo, la cadena basada en código hexadecimal `0x8,0x11` para un procesador ARM Cortex A57 especifica el mismo 
conjunto de eventos que el de la cadena` instr, cycles`.
Claramente, el último formato es mucho más intuitivo que el anterior; el usuario probablemente puede adivinar que estamos tratando de 
especificar los eventos de hardware "instrucciones retiradas" y "ciclos".

La opción `-V` hace posible especificar un conjunto de _virtual counters_. Los sistemas modernos permiten monitorear un conjunto de
eventos de hardware utilizando PMC. Aún así, otra información de monitoreo (por ejemplo, consumo de energía) puede estar expuesta al 
sistema operativo por otros medios, como registros de función fija, sensores, etc. Estos datos "no PMC" son expuestos por el módulo del
kernel de PMCTrack como _virtual counters_, En lugar de eventos HW. Los módulos de monitoreo de PMCTrack se encargan de implementar el
acceso de bajo nivel a los contadores virtuales. Para recuperar la lista de eventos HW exportados por el módulo de monitoreo activo, 
use `pmc-events -V`. Puede encontrar más información sobre los módulos de monitoreo de PMCTrack en una sección separada de este 
documento.

El comando `pmctrack` permite  tres modos de uso:

1. **Muestreo basado en tiempo ó Time-Based Sampling (TBS)**: los valores de contador virtual y PMC para una aplicación determinada se recopilan a intervalos de tiempo
regulares.
1. **Muestreo basado en eventos ó Event-Based Sampling (EBS)**: los valores de PMC y de contador virtual para una aplicación se r
ecopilan cada vez que un evento PMC determinado alcanza un recuento determinado.
2. **Modo de monitoreo en todo el sistema basado en el tiempo ó Time-Based system-wide monitoring mode**: Este modo es una variante 
del modo TBS, pero se proporciona información de monitoreo para cada CPU en el sistema, en lugar de para una aplicación específica. Este modo se puede habilitar con el interruptor `-S`

Para ilustrar cómo funciona el modo TBS, consideremos el siguiente comando de ejemplo invocado en un sistema con un procesador Intel Xeon Haswell de cuatro núcleos:

	$ pmctrack  -T 1  -c instr,llc_misses -V energy_core  ./mcf06
	[Event-to-counter mappings]
	pmc0=instr
	pmc3=llc_misses
	virt0=energy_core
	[Event counts]
	nsample    pid      event          pmc0           pmc3         virt0
	      1  10767       tick    2017968202       30215772       7930969
	      2  10767       tick    1220639346       24866936       7580993
	      3  10767       tick    1204660012       24726068       7432617
	      4  10767       tick    1524589394       20013147       8411560
	      5  10767       tick    1655802083        9520886       8531860
	      6  10767       tick    2555712483       18420142       6615844
	      7  10767       tick    2222232510       19594864       6385986
	      8  10767       tick    1348937378       22795510       5966308
	      9  10767       tick    1455948820       22282935       5994934
	     10  10767       tick    1324007762       22682355       5951354
	     11  10767       tick    1345928005       22477525       5968872
	     12  10767       tick    1345868008       22400733       6024780
	     13  10767       tick    1370194276       22121318       6024169
	     14  10767       tick    1329712408       22154371       6030700 
	     15  10767       tick    1365130132       21859147       6076293 
	     16  10767       tick    1315829803       21780616       6136962 
	     17  10767       tick    1357349957       20889360       6234619 
	     18  10767       tick    1377910047       19539232       6519897 
	...

Este comando proporciona al usuario el número de instrucciones retiradas, las fallas de caché de último nivel (LLC) 
y el consumo de energía del núcleo (en uJ) cada segundo. El comienzo de la salida del comando muestra la asignación de evento-contador 
para los diversos eventos de hardware y contadores virtuales. La sección "Event counts" en la salida muestra una tabla con los recuentos
sin procesar de los diversos eventos; Cada muestra (una por segundo) está representada por una fila diferente. Tenga en cuenta que el
período de muestreo se especifica en segundos a través de la opción -T; también se pueden especificar fracciones de segundo (por 
ejemplo, 0,3 para 300 ms). Si el usuario incluye la opción -A en la línea de comando, `pmctrack` mostrará en su lugar el conteo agregado 
de eventos para toda la ejecución de la aplicación. Al final de la línea, especificamos el comando para ejecutar la aplicación asociada 
que deseamos monitorear (por ejemplo: ./mcf06).

En caso de que un modelo de procesador específico no integre suficientes PMC para monitorear un conjunto dado de eventos a la vez,
el usuario puede recurrir a la función de multiplexación de eventos de PMCTrack. Esto se reduce a especificar varios conjuntos de 
eventos mediante la inclusión de varias instancias de la opción-c en la línea de comandos. En este caso, los diversos conjuntos de 
eventos se recopilarán Round-Robin y un nuevo campo `expid` en la salida indicará el conjunto de eventos al que pertenece 
una muestra en particular. En una línea similar, el muestreo basado en el tiempo también es compatible con aplicaciones multiproceso.
En este caso, las muestras de cada subproceso en la aplicación se identificarán con un valor diferente en la columna pid.

El muestreo basado en eventos (EBS) constituye una variante del muestreo basado en el tiempo en el que los valores de PMC se 
recopilan cuando un determinado recuento de eventos alcanza un cierto umbral (threshold) _T_. Para admitir EBS, el módulo del kernel de
PMCTrack explota la función de interrupción en el desbordamiento presente en la mayoría de las Unidades de Monitoreo de Rendimiento (PMU)
modernas. Para usar la función EBS desde el espacio de usuario, el indicador "ebs" debe especificarse en la línea de comando `pmctrack` 
con el nombre de un evento. Al hacerlo, también se puede especificar un valor de umbral como en el siguiente ejemplo:


	$ pmctrack -c instr:ebs=500000000,llc_misses -V energy_core  ./mcf06 
	[Event-to-counter mappings]
	pmc0=instr
	pmc3=llc_misses
	virt0=energy_core
	[Event counts]
	nsample    pid      event          pmc0          pmc3         virt0
	      1  10839        ebs     500000078        892837        526489 
	      2  10839        ebs     500000047       9383500       1946166 
	      3  10839        ebs     500000050       9692922       2544250 
	      4  10839        ebs     500000007      10017122       2818298 
	      5  10839        ebs     500000012       9907918       3055236 
	      6  10839        ebs     500000011      10335579       3108215 
	      7  10839        ebs     500000046      10735151       3118713 
	      8  10839        ebs     500000011      10335980       3119140 
	      9  10839        ebs     500000004      10250777       3053100 
	     10  10839        ebs     500000019      11382679       2997802 
	     11  10839        ebs     500000035       6650139       2587890 
	     12  10839        ebs     500000004        474847       2596313 
	     13  10839        ebs     500000039        532301       2601074 
	     14  10839        ebs     500000019        577618       2617187 
	     15  10839        ebs     500000062       6221112       2442504 
	     16  10839        ebs     500000037       9177684       2325317 
	     17  10839        ebs     500000058       2697348       1106994 
	     18  10839        ebs     500000006       3520781       1264404 
	     19  10839        ebs     500000055       2777145       1119934 
	     20  10839        ebs     500000034       1965457        964843 
	     21  10839        ebs     500000004       2290861       1035095 
	     22  10839        ebs     500000011       3276917       1217895 
	     23  10839        ebs     500000041       4202958       1409973 
	     24  10839        ebs     500000034       5343461       1608947
	...

Las columnas `pmc3` y` virt0` muestran el número de pérdidas LLC y el consumo de energía cada 500 millones de instrucciones retiradas. 
Sin embargo, tenga en cuenta que los valores en la columna `pmc0` no reflejan exactamente el recuento de instrucciones de destino. Esto 
tiene que ver con el hecho de que, en los procesadores modernos, la interrupción de la PMU no se atiende inmediatamente después de que el
contador se desborda. En su lugar, debido a la ejecución fuera de orden y especulativa, se pueden ejecutar varias docenas de 
instrucciones o más dentro del período transcurrido desde el desbordamiento del contador hasta que la aplicación se interrumpa realmente. Estas inexactitudes no plantean un gran problema siempre que se utilicen ventanas de instrucciones groseras.

### Libpmctrack

Otra forma de acceder a la funcionalidad de PMCTrack desde el espacio de usuario es a través de _libpmctrack_. Esta biblioteca permite 
caracterizar el rendimiento de fragmentos de código específicos a través de PMC y contadores virtuales en programas secuenciales y de 
multiproceso escritos en C o C ++. La API de Libpmctrack permite indicar la configuración deseada de PMC y del contador virtual al módulo
de kernel de PMCTrack en cualquier punto del código de la aplicación o dentro de un sistema de tiempo de ejecución. El programador puede 
recuperar los recuentos de eventos asociados para cualquier fragmento de código (a través de TBS o EBS) simplemente encerrando el código
entre invocaciones a las funciones `pmctrack_start_count * ()` y `pmctrack_stop_count ()`. Para ilustrar el uso de libpmctrack, se 
proporcionan varios programas de ejemplo en el repositorio en `test / test_libpmctrack`.

## módulos de monitoreo de PMCTrack

El módulo de kernel de PMCTrack se puede ampliar fácilmente con soporte para funciones de monitoreo HW adicionales qs / pmcs`.

Desde el punto de vista del programador, la creación de un módulo de monitoreo implica la implementación de la interfaz `Monitoring_module_t` (` <pmc / monitoring_mod.h> `) en un archivo .c separado de las fuentes del módulo del kernel PMCTrack. La interfaz `Monitoring_module_t` consta de varias funciones de devolución de llamada que permiten notificar al módulo sobre las activaciones / desactivaciones solicitadas por el administrador del sistema, en los cambios de contexto de los hilos, cada vez que un hilo entra / sale del sistema, etc. El programador generalmente implementa solo el subconjunto de retrollamadas requeridas para realizar el procesamiento interno necesario. En particular, cualquier tipo de información de monitoreo a la que acceda el módulo de monitoreo puede ser expuesta a las herramientas de usuario de PMCTrack como un contador virtual.

El módulo del kernel de PMCTrack también permite que los módulos de monitoreo tomen el control total de los contadores de monitoreo de rendimiento para realizar cualquier tipo de tarea interna. Para hacer esto posible, el desarrollador del módulo de monitoreo no tiene que lidiar directamente con los registros del contador de rendimiento. En su lugar, el programador indica la configuración de contador deseada (codificada en una cadena) utilizando una función API. Cada vez que se recopilan nuevas muestras de PMC para un subproceso, se invoca una función de devolución de llamada del módulo de monitoreo, pasando las muestras como un parámetro. Gracias a esta función, un módulo de monitoreo solo accederá a los registros de bajo nivel para proporcionar al programador o al usuario final otra información de monitoreo de hardware que no esté modelada como eventos PMC, como la temperatura o el consumo de energía.

PMCTrack puede incluir varios módulos de monitoreo compatibles con una plataforma dada. Sin embargo, solo se puede habilitar uno a la vez. Los módulos de monitoreo disponibles para el sistema actual se pueden obtener leyendo del archivo `/ proc / pmc / mm_manager`:

	$ cat /proc/pmc/mm_manager 
	[*] 0 - This is just a proof of concept
	[ ] 1 - IPC sampling-based SF estimation model
	[ ] 2 - PMCtrack module that supports Intel CMT
	[ ] 3 - PMCtrack module that supports Intel RAPL
  
En el ejemplo anterior, se enumeran cuatro módulos de monitoreo y el módulo # 0, marcado con "*", es el módulo de monitoreo _active_.

En caso de que existan varios módulos de monitoreo compatibles, el administrador del sistema puede decirle al sistema cuál usar escribiendo en el archivo `/ proc / pmc / mm_manager` de la siguiente manera:

      $ echo 'activate 3' > /proc/pmc/mm_manager
      $ cat /proc/pmc/mm_manager 
      [ ] 0 - This is just a proof of concept
      [ ] 1 - IPC sampling-based SF estimation model
      [ ] 2 - PMCtrack module that supports Intel CMT
      [*] 3 - PMCtrack module that supports Intel RAPL

El comando `pmc-events` puede usarse para enumerar los contadores virtuales exportados por el módulo de monitoreo activo, de la siguiente manera:
```bash
  $ pmc-events -V
    [Virtual counters]
    energy_pkg
    energy_dram
```
## Usando PMCTrack desde el programador del sistema operativo

PMCTrack permite que cualquier algoritmo de planificación en el kernel de Linux (es decir, clase de planificación) recopile 
datos de monitoreo por hilo ó thread, lo que hace posible impulsar decisiones de planificación basadas en el comportamiento de la memoria
de las tareas u otras propiedades de microarquitectura. Activar este modo para un hilo en particular desde el código del planificador 
se reduce a activar el indicador `prof_enabled` en el descriptor del hilo. Este indicador se agrega a la estructura de tareas de Linux 
(task structure) al aplicar el parche de kernel de PMCTrack.

Para garantizar que la implementación del algoritmo de planificación que se beneficia de esta característica siga siendo independiente
de la arquitectura, el planificador en sí (implementado en el núcleo) no configura ni trata directamente los contadores de rendimiento. 
En su lugar, el módulo de monitoreo activo en PMCTrack está a cargo de alimentar la política de planificación con las métricas de 
monitoreo de rendimiento de alto nivel necesarias, como la relación de instrucción por ciclo de una tarea o su tasa de errores de 
caché de último nivel.

El planificador puede comunicarse con el módulo de monitoreo activo para obtener datos por hilo a través de la siguiente función de 
la API del kernel de PMCTrack:

```c

	int pmcs_get_current_metric_value( struct task_struct* task, int metric_id,
									   uint64_t* value );
```

Para simplificar, a cada métrica se le asigna una ID identificación numérica, conocida por el planificador y el módulo de monitoreo. 
Para obtener el valor actualizado de una métrica específica, la función mencionada anteriormente puede invocarse desde la función de 
procesamiento de tics en el planificador.

Los módulos de monitoreo hacen posible que una política de planificación basada en PMC o métricas de contador virtual se amplíe 
sin problemas a nuevas arquitecturas o modelos de procesador, siempre que el hardware permita recopilar los datos de monitoreo 
necesarios. Todo lo que debe hacerse es construir un módulo de monitoreo o adaptar uno existente a la plataforma en cuestión.
