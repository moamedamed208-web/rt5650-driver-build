# RT5650 SPB KMDF Filter Driver

ده مشروع KMDF filter driver يستهدف الجهاز:

- `ACPI\10EC5650`
- Realtek RT5650 audio codec

الـ driver بيعمل الآتي:

1. يتثبت كـ **device-specific lower filter** على الجهاز.
2. في `EvtDevicePrepareHardware` يقرأ الـ translated resources ويستخرج **I2C/SPB connection ID**.
3. يفتح remote I/O target على الـ SPB controller عبر Resource Hub.
4. عند دخول الجهاز `D0` يطبق الترتيب التالي باستخدام `read -> update bits -> write`:

```c
rt_update(0x2A, 0x4040, 0x0000);
rt_update(0x46, 0x0002, 0x0000);
rt_update(0x47, 0x0002, 0x0000);
rt_update(0x48, 0x2001, 0x0000);
rt_update(0x01, 0xC0C0, 0x0000);
rt_update(0x01, 0xFFFF, 0x1818);
```

## ملفات المشروع

- `Rt5650SpbFilter.c` : كود الدرايفر
- `Rt5650SpbFilter.inf` : Extension INF للتثبيت على Windows 10/11 x64
- `Rt5650SpbFilter.vcxproj` / `Rt5650SpbFilter.sln` : مشروع Visual Studio + WDK
- `scripts\Enable-TestSigning.bat` : تفعيل Test Signing
- `scripts\Build-Release.cmd` : build سريع من CLI
- `scripts\Prepare-Package.ps1` : تجميع ملفات الحزمة في مجلد `package`
- `scripts\Make-TestCert-And-Sign.ps1` : إنشاء test cert وتوقيع الـ sys/.cat
- `scripts\Install-Driver.ps1` : تثبيت الحزمة بـ pnputil

## المتطلبات

- Visual Studio 2022
- Windows Driver Kit (WDK) 10
- الهدف: x64
- Windows 10 1903+ أو Windows 11

> الـ INF هنا من نوع **Extension INF** ويستخدم `AddFilter` لتسجيل فلتر خاص بالجهاز على Windows 10 1903+.

## البناء

### من Visual Studio

1. افتح `Rt5650SpbFilter.sln`
2. اختَر:
   - Configuration = `Release`
   - Platform = `x64`
3. اعمل **Build Solution**

بعد الـ build شغّل:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Prepare-Package.ps1
```

وده هيجمع ملفات الحزمة داخل:

- `package\Rt5650SpbFilter.sys`
- `package\Rt5650SpbFilter.inf`
- `package\Rt5650SpbFilter.cat` (لو اتولد أثناء الـ build)

### من سطر الأوامر

شغّل Developer Command Prompt الخاصة بـ VS/WDK ثم:

```bat
scripts\Build-Release.cmd
```

## التوقيع

### مهم

على Windows x64 **مش هينفع تحميل kernel driver unsigned بالكامل** بشكل طبيعي.

الحل العملي للتجربة:

1. تفعيل **test signing mode**
2. عمل **test certificate**
3. توقيع `Rt5650SpbFilter.sys` و `Rt5650SpbFilter.cat`

### 1) تفعيل test signing

شغّل كمسؤول:

```bat
scripts\Enable-TestSigning.bat
```

ثم أعد التشغيل.

> لو Secure Boot شغال، غالباً هتحتاج تعطله علشان test mode يشتغل.

### 2) توقيع الحزمة

بعد الـ build وتجهيز الـ package، شغّل PowerShell كمسؤول:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Make-TestCert-And-Sign.ps1
```

## التثبيت

بعد الـ build والتوقيع:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Install-Driver.ps1
```

أو يدويًا:

```bat
pnputil /add-driver .\package\Rt5650SpbFilter.inf /install
```

بعدها:

- أعد التشغيل
- أو Disable / Enable للجهاز من Device Manager

## Debug / تحقق

- راقب `DbgView` أو kernel debugger لرسائل:
  - `Rt5650SpbFilter: DriverEntry`
  - `Rt5650SpbFilter: init sequence applied successfully`

## ملاحظات مهمة

1. لو الـ ACPI resources للجهاز **لا تحتوي I2C connection resource**، الدرايفر هيرجع `STATUS_NOT_FOUND`.
2. لو الـ stack الحالي للجهاز عنده تعارض مع ترتيب الفلاتر، ممكن نغيّر `FilterPosition` من `Lower` إلى `Upper` في الـ INF.
3. لو محتاج retry/delay بين أوامر الـ registers، ممكن أضيفه بسهولة.
4. لو محتاج نفس المنطق لكن كـ **function driver** بدل filter driver، أقدر أطلع لك نسخة تانية.
