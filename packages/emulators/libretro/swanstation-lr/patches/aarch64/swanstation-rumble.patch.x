--- a/src/libretro/libretro_host_interface.cpp	2024-05-31 05:13:49.000000000 +0900
--- b/src/libretro/libretro_host_interface.cpp	2024-06-15 02:42:35.867091385 +0900
@@ -32,6 +32,10 @@
 #include <file/file_path.h>
 #include <streams/file_stream.h>
 
+#include <stdio.h>
+#include <unistd.h>
+#include <errno.h>
+
 Log_SetChannel(LibretroHostInterface);
 
 #ifdef WIN32
@@ -1203,6 +1207,15 @@ void LibretroHostInterface::OnRunningGam
 void LibretroHostInterface::InitRumbleInterface()
 {
   m_rumble_interface_valid = g_retro_environment_callback(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &m_rumble_interface);
+  // Check write access for duty cycle for rk3566
+  char* filepath = "/sys/class/pwm/pwmchip1/pwm0/duty_cycle";
+  int returnval;
+  returnval = access (filepath, F_OK);
+  if (returnval == 0){
+    returnval = access (filepath, W_OK);
+    if (errno == EACCES)
+      system("sudo chmod 777 /sys/class/pwm/pwmchip1/pwm0/duty_cycle &");
+  }
 }
 
 void LibretroHostInterface::UpdateControllers()
@@ -1335,6 +1348,34 @@ void LibretroHostInterface::UpdateContro
   {
     const u16 strong = static_cast<u16>(static_cast<u32>(controller->GetVibrationMotorStrength(0) * 65535.0f));
     const u16 weak = static_cast<u16>(static_cast<u32>(controller->GetVibrationMotorStrength(1) * 65535.0f));
+    FILE *file;
+
+    if (strong > 0){
+       if ((file = fopen("/sys/class/pwm/pwmchip0/pwm0/duty_cycle", "r+"))) {
+          fputs("10", file);
+          fclose(file);
+       } else if ((file = fopen("/sys/class/pwm/pwmchip1/pwm0/duty_cycle", "r+"))) {
+          fputs("10", file);
+          fclose(file);
+       }
+    } else if (weak > 0){
+       if ((file = fopen("/sys/class/pwm/pwmchip0/pwm0/duty_cycle", "r+"))) {
+          fputs("500000", file);
+          fclose(file);
+       } else if ((file = fopen("/sys/class/pwm/pwmchip1/pwm0/duty_cycle", "r+"))) {
+          fputs("500000", file);
+          fclose(file);
+       }
+    } else {
+       if ((file = fopen("/sys/class/pwm/pwmchip0/pwm0/duty_cycle", "r+"))) {
+          fputs("1000000", file);
+          fclose(file);
+       } else if ((file = fopen("/sys/class/pwm/pwmchip1/pwm0/duty_cycle", "r+"))) {
+          fputs("1000000", file);
+          fclose(file);
+       }
+    }
+
     m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_STRONG, strong);
     m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_WEAK, weak);
   }
