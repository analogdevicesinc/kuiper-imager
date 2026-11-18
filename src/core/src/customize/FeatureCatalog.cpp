#include "kuiper/ImageCustomizer.hpp"

// The feature catalog: the data mirroring the Kuiper build that turns a basic
// image into a full one. Provenance (which stages each block comes from), the
// vendored-asset model, and the two reviewed deviations (-dbgsym omitted;
// adi-scripts already in the base image) are in docs: customize-pipeline. The
// inline tags below name each block's source stage. Should become CI-generated.

namespace kuiper {

const std::vector<FeatureDef>& featureCatalog() {
    static const std::vector<FeatureDef> kCatalog = {
        // ---- Desktop (desktop-env + vnc-server + cosmetic) -------------------
        {
            "desktop", "XFCE Desktop + VNC", "Desktop",
            "XFCE4 desktop, display manager, headless X server and VNC.",
            {// 01.desktop-env (packages-with-recommends)
             "lightdm", "xfce4", "desktop-base", "at-spi2-core", "dbus-x11",
             "accountsservice", "xorg", "xvfb", "gnome-terminal",
             "gnome-themes-extra", "network-manager-gnome", "wpasupplicant",
             "wireless-tools", "tumbler", "polkitd", "pkexec", "plymouth",
             "chromium",
             // 02.vnc-server (packages-with-recommends)
             "x11vnc", "xserver-xorg-video-dummy"},
            /*withRecommends=*/true,
            {
                // 01.desktop-env tweaks
                "apt-get purge -y xiccd colord colord-data ghostscript || true",
                "sed -i 's/#autologin-user=/autologin-user=analog/g' "
                "/etc/lightdm/lightdm.conf || true",
                "sed -i 's/#autologin-user-timeout=0/autologin-user-timeout=0/g' "
                "/etc/lightdm/lightdm.conf || true",
                "sed -i 's/#logind-check-graphical=false/"
                "logind-check-graphical=true/g' /etc/lightdm/lightdm.conf || true",
                "sed -i 's/^workspace_count=.*$/workspace_count=1/g' "
                "/usr/share/xfwm4/defaults || true",
                "echo 'export CHROMIUM_FLAGS=\"$CHROMIUM_FLAGS "
                "--password-store=basic\"' >> /etc/chromium.d/default-flags || true",
                // 02.vnc-server setup
                "mkdir -p /home/analog/.vnc",
                "x11vnc -storepasswd analog /home/analog/.vnc/passwd && "
                "chmod 644 /home/analog/.vnc/passwd",
                "chown -R analog:analog /home/analog/.vnc || true",
                "sed -i 's/allowed_users=console/allowed_users=anybody/g' "
                "/etc/X11/Xwrapper.config || true",
                "systemctl enable xserver",
                "systemctl enable x11vnc",
                // 03.cosmetic tweaks
                "sed -i 's/Xfce/Adwaita-dark/g' "
                "/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml || true",
                "printf '%s\\n' '[org.gnome.Terminal.Legacy.Settings]' "
                "\"theme-variant='dark'\" > "
                "/usr/share/glib-2.0/schemas/90_gnome-terminal.gschema.override && "
                "glib-compile-schemas /usr/share/glib-2.0/schemas/ || true",
                "rm -f /usr/share/backgrounds/xfce/xfce-x.svg && "
                "ln -sf /usr/share/adi-wallpaper/wallpaper.png "
                "/usr/share/backgrounds/xfce/xfce-x.svg || true",
                "sed -i '/exec ${XFCE4_SESSION_COMPOSITOR:-xfce4-session}/i "
                "xset s off\\nxset -dpms\\nxset s noblank' "
                "/etc/xdg/xfce4/xinitrc || true",
                "sed -i 's/Name=Science/Name=Analog Devices Tools/g' "
                "/usr/share/desktop-directories/xfce-science.directory || true",
                "sed -i '/<Category>Science/{n;n;s/^/<Exclude><Category>"
                "Development<\\/Category><\\/Exclude>\\n/}' "
                "/etc/xdg/menus/xfce-applications.menu || true",
            },
            1500,
            {},  // archs: all
            {    // files: vendored verbatim, embedded via kuiper_assets.qrc
                {"/lib/systemd/system/x11vnc.service",
                 ":/kuiper/desktop/x11vnc.service", false, {}},
                {"/lib/systemd/system/xserver.service",
                 ":/kuiper/desktop/xserver.service", false, {}},
                {"/usr/bin/adi-xserver.sh", ":/kuiper/desktop/adi-xserver.sh",
                 true, {}},
                // RPi5 vc4 modesetting: arm64 only (as in the build).
                {"/etc/X11/xorg.conf.d/99-vc4.conf",
                 ":/kuiper/desktop/99-vc4.conf", false, {"arm64"}},
                // Cosmetic assets (03.cosmetic).
                {"/usr/share/adi-wallpaper/wallpaper.png",
                 ":/kuiper/desktop/wallpaper.png", false, {}},
                {"/usr/share/icons/hicolor/16x16/apps/"
                 "org.xfce.panel.applicationsmenu.png",
                 ":/kuiper/desktop/org.xfce.panel.applicationsmenu.png", false, {}},
            },
        },
        // ---- Libraries -------------------------------------------------------
        {
            "libiio", "libiio", "Libraries",
            "ADI library for interfacing with Linux IIO devices (+ iiod).",
            {"libiio0", "libiio-dev", "libiio-utils", "python3-libiio", "iiod"},
            false, {}, 40, {}, {},
        },
        {
            "libm2k", "libm2k", "Libraries",
            "Library for interfacing with the ADALM2000.",
            {"libm2k", "libm2k-csharp", "libm2k-dev", "libm2k-tools",
             "python3-libm2k"},
            false, {}, 30, {}, {},
        },
        {
            "libad", "libad9361 / libad9166", "Libraries",
            "ADI userspace libraries for the AD9361 and AD9166.",
            {"libad9361", "libad9361-dev", "python3-libad9361", "libad9166",
             "libad9166-dev", "python3-libad9166"},
            false, {}, 20, {}, {},
        },
        {
            "pyadi", "pyADI-IIO", "Libraries",
            "ADI Python interfaces for IIO-driver hardware (via pip).",
            {"python3-numpy"},  // python3-pip is already in the base image
            false,
            {"pip3 install pyadi-iio --break-system-packages"},
            260, {}, {},
        },
        // ---- Applications ----------------------------------------------------
        {
            "iio-oscilloscope", "IIO Oscilloscope", "Applications",
            "GTK application to visualize IIO devices.",
            {"iio-oscilloscope"}, false, {}, 180, {}, {},
        },
        {
            "scopy", "Scopy", "Applications",
            "Software oscilloscope and signal-analysis toolset.",
            {"scopy"}, false, {}, 400, {}, {},
        },
        {
            "gnuradio", "GNU Radio", "Applications",
            "Signal-processing toolkit for software-defined radio.",
            {"gnuradio", "gnuradio-dev"},
            false,
            {"sed -i 's|^Exec=gnuradio-companion|Exec=env "
             "PYTHONPATH=/usr/lib/python3/dist-packages:$PYTHONPATH "
             "gnuradio-companion|' /usr/share/applications/gnuradio-grc.desktop "
             "|| true",
             "echo \"alias gnuradio-companion='PYTHONPATH="
             "/usr/lib/python3/dist-packages:\\$PYTHONPATH gnuradio-companion'\" "
             ">> /etc/bash.bashrc || true"},
            900, {}, {},
        },
        {
            "gr-m2k", "gr-m2k", "Applications",
            "GNU Radio blocks for the ADALM-2000.",
            {"gr-m2k", "gr-m2k-dev", "python3-gr-m2k"}, false, {}, 50, {}, {},
        },
        {
            "iio-fm-radio", "IIO FM Radio", "Applications",
            "Simple IIO FM-radio receive example.",
            {"iio-fm-radio"}, false, {}, 5, {}, {},
        },
        {
            "fru-tools", "FRU Tools", "Applications",
            "Tools to display/manipulate FMC FRU info.",
            {"fru-tools"}, false, {}, 5, {}, {},
        },
        {
            "jesd-eye-scan-gtk", "JESD Eye Scan", "Applications",
            "JESD204 eye-scan visualization utility.",
            {"jesd-eye-scan-gtk"}, false, {}, 20, {}, {},
        },
        {
            "colorimeter", "Colorimeter", "Applications",
            "Application for the EVAL-CN0363-PMDZ.",
            {"colorimeter"}, false, {}, 10, {}, {},
        },
    };
    return kCatalog;
}

}  // namespace kuiper
