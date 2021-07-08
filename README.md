# chiagen
standalone chia generator

![alt text](https://raw.githubusercontent.com/uraymeiviar/chiagen/master/img/screenshot.JPG)

![alt text](https://raw.githubusercontent.com/uraymeiviar/chiagen/master/img/screenshot2.JPG)

this is front-end of [madMAx43v3r](https://github.com/madMAx43v3r/chia-plotter) and also [chiapos reference plotter](https://github.com/Chia-Network/chiapos), but not limited to it, more plotter or tools will be added with gui frontend for each tools

this program does not wrap those plotter program, it compile those plotter code inside it, so each ploting will be run on separate thread instead of separate process, I did that so i will have more control over it like triggering an event when a specific phase is done, and also to make it possible in the future to stop or pause the ploting process

this app also will add functionality as plot manager, where user can make a schedule or arrangement when to launch new process, each job launching can be triggered by an event sent by another job

you can also start the program in CLI mode, just run it on command prompt and add extra parameter `D:\chiagen.exe help`

![alt text](https://raw.githubusercontent.com/uraymeiviar/chiagen/master/img/cli-help.jpg)

to get help for each command, continue with `D:\chiagen.exe create help

![alt text](https://raw.githubusercontent.com/uraymeiviar/chiagen/master/img/cli-create.jpg)

also the green rectangle are hints, those are the only parameters you need to specify for creating plot for pool using madmax plotter

for those who want to create pool plot using madmax by GUI, here are the parameters you need to specify

![alt text](https://raw.githubusercontent.com/uraymeiviar/chiagen/master/img/pool-plot-gui.jpg)

just leave pool key empty, but fill the puzzle hash field

----

these are planned feature
* **(done)** integrated CLI/GUI 
* proper Pause/Resume on ploting process (its not pause as in close app and restart pc continue later)
* proper Cancel or stop then clean temporary files
* plot manager where each job launching can be triggered by an event sent by another job, these are Staring Rule for each job
   * start paused
   * start delayed 
   * start with conditional
      * if current running job is less than specific number
      * only run on specific time range
      * run only if disk has free space more than specific amount
   * start based on event
      * the event are triggered by another job, mostly the events are starting job, finished job, finished phase, job crash etc
* system monitoring
  * realtime CPU usage
  * realtime memory usage
  * realtime disk operation
  * drive space
  * drive remaining life
  * drive/cpu/mem temperature
* tools other than ploting, like verifying plots
* chia network stat (netspace, block num, tx status)
* probably farmer or harvester integrated into the app, when official pool protocols are done
* web interface for remotely managing it via browser
* and also plotter improvement itself
