#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLUEZ_BUS_NAME      "org.bluez"
#define ADAPTER_PATH        "/org/bluez/hci0"
#define GATT_MANAGER_IFACE "org.bluez.GattManager1"

#define ITRISERVICE_UUID         "0000fff0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_RX   "0000fff5-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_TX   "0000fff4-0000-1000-8000-00805f9b34fb"

#define APP_PATH                 "/org/bluez/app"
#define SERVICE_PATH             "/org/bluez/app/service_itri"
#define CHAR_RX_PATH             "/org/bluez/app/service_itri/char_rx"
#define CHAR_TX_PATH             "/org/bluez/app/service_itri/char_tx"

static char tx_buffer[2048] = "Ready for commands...";
static char rx_buffer[512] = "";

static void send_notify(GDBusConnection *conn, const char *msg) {
    snprintf(tx_buffer, sizeof(tx_buffer), "%s", msg);
    printf("===========================================\n");
    printf("[Pi TX Notify 送出]: %s\n", tx_buffer);
    printf("===========================================\n");

    GVariantBuilder array_builder;
    g_variant_builder_init(&array_builder, G_VARIANT_TYPE("ay"));
    for (size_t i = 0; i < strlen(tx_buffer); i++) {
        g_variant_builder_add(&array_builder, "y", (guchar)tx_buffer[i]);
    }

    GVariantBuilder prop_builder;
    g_variant_builder_init(&prop_builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&prop_builder, "{sv}", "Value", g_variant_builder_end(&array_builder));

    GVariantBuilder inval_builder;
    g_variant_builder_init(&inval_builder, G_VARIANT_TYPE("as"));

    g_dbus_connection_emit_signal(
        conn,
        NULL,
        CHAR_TX_PATH,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1", &prop_builder, &inval_builder),
        NULL
    );
}

static void exec_sys_cmd(const char *cmd, char *output, size_t max_len) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(output, max_len, "Exec failed: %s", cmd);
        return;
    }
    size_t len = fread(output, 1, max_len - 1, fp);
    output[len] = '\0';
    pclose(fp);

    while (len > 0 && (output[len - 1] == '\n' || output[len - 1] == '\r')) {
        output[--len] = '\0';
    }
}

static void process_ble_command(GDBusConnection *conn, const char *cmd) {
    char response[2048] = {0};

    if (strncmp(cmd, "GET_INFO", 8) == 0) {
        char ip[128] = "127.0.0.1";
        char ssid[128] = "Disconnected";

        exec_sys_cmd("hostname -I | awk '{print $1}'", ip, sizeof(ip));
        if (strlen(ip) == 0) strcpy(ip, "No IP");

        exec_sys_cmd("iwgetid -r", ssid, sizeof(ssid));
        if (strlen(ssid) == 0) strcpy(ssid, "Not Connected");

        snprintf(response, sizeof(response), "INFO:%s;%s", ip, ssid);
    }
    else if (strncmp(cmd, "SCAN_WIFI", 9) == 0) {
        char wifi_raw[1024] = {0};
        exec_sys_cmd("sudo nmcli -t -f SSID dev wifi list | grep -v '^$' | sort -u | head -n 10 | paste -sd ',' -", wifi_raw, sizeof(wifi_raw));

        if (strlen(wifi_raw) == 0) {
            snprintf(response, sizeof(response), "WLAN_LIST:No Wi-Fi Found");
        } else {
            snprintf(response, sizeof(response), "WLAN_LIST:%s", wifi_raw);
        }
    }
    else if (strncmp(cmd, "CONNECT:", 8) == 0) {
        char *target_ssid = NULL;
        char *target_pass = NULL;

        char cmd_copy[256];
        strncpy(cmd_copy, cmd + 8, sizeof(cmd_copy) - 1);

        target_ssid = strtok(cmd_copy, ":");
        target_pass = strtok(NULL, ":");

        if (target_ssid) {
            char nmcli_cmd[512];
            if (target_pass && strlen(target_pass) > 0) {
                snprintf(nmcli_cmd, sizeof(nmcli_cmd), "sudo nmcli dev wifi connect '%s' password '%s'", target_ssid, target_pass);
            } else {
                snprintf(nmcli_cmd, sizeof(nmcli_cmd), "sudo nmcli dev wifi connect '%s'", target_ssid);
            }

            char res_out[256] = {0};
            exec_sys_cmd(nmcli_cmd, res_out, sizeof(res_out));

            if (strstr(res_out, "successfully activated") || strstr(res_out, "成功")) {
                snprintf(response, sizeof(response), "CONNECT_OK");
            } else {
                snprintf(response, sizeof(response), "CONNECT_FAIL:%s", res_out);
            }
        } else {
            snprintf(response, sizeof(response), "CONNECT_FAIL:Invalid Command Format");
        }
    }
    else {
        exec_sys_cmd(cmd, response, sizeof(response));
    }

    send_notify(conn, response);
}

static void handle_object_manager_method_call(GDBusConnection *conn,
                                              const gchar *sender,
                                              const gchar *object_path,
                                              const gchar *interface_name,
                                              const gchar *method_name,
                                              GVariant *parameters,
                                              GDBusMethodInvocation *invocation,
                                              gpointer user_data) {

    if (g_strcmp0(method_name, "GetManagedObjects") == 0) {
        printf("\n[Pi D-Bus Log] 手機與 Pi 建立 GATT 連線，BlueZ 正在讀取服務結構 (GetManagedObjects)...\n");

        GVariantBuilder b_app;
        g_variant_builder_init(&b_app, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

        GVariantBuilder b_svc_ifaces, b_svc_props;
        g_variant_builder_init(&b_svc_ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_init(&b_svc_props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&b_svc_props, "{sv}", "UUID", g_variant_new_string(ITRISERVICE_UUID));
        g_variant_builder_add(&b_svc_props, "{sv}", "Primary", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&b_svc_ifaces, "{sa{sv}}", "org.bluez.GattService1", &b_svc_props);
        g_variant_builder_add(&b_app, "{oa{sa{sv}}}", SERVICE_PATH, &b_svc_ifaces);

        GVariantBuilder b_rx_ifaces, b_rx_props;
        g_variant_builder_init(&b_rx_ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_init(&b_rx_props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&b_rx_props, "{sv}", "UUID", g_variant_new_string(CHARACTERISTIC_UUID_RX));
        g_variant_builder_add(&b_rx_props, "{sv}", "Service", g_variant_new_object_path(SERVICE_PATH));
        const gchar *rx_flags[] = {"write", "write-without-response", NULL};
        g_variant_builder_add(&b_rx_props, "{sv}", "Flags", g_variant_new_strv(rx_flags, -1));
        g_variant_builder_add(&b_rx_ifaces, "{sa{sv}}", "org.bluez.GattCharacteristic1", &b_rx_props);
        g_variant_builder_add(&b_app, "{oa{sa{sv}}}", CHAR_RX_PATH, &b_rx_ifaces);

		GVariantBuilder b_tx_ifaces, b_tx_props;
		g_variant_builder_init(&b_tx_ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
		g_variant_builder_init(&b_tx_props, G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(&b_tx_props, "{sv}", "UUID", g_variant_new_string(CHARACTERISTIC_UUID_TX));
		g_variant_builder_add(&b_tx_props, "{sv}", "Service", g_variant_new_object_path(SERVICE_PATH));
		const gchar *tx_flags[] = {"read", "notify", NULL};
		g_variant_builder_add(&b_tx_props, "{sv}", "Flags", g_variant_new_strv(tx_flags, -1));
		g_variant_builder_add(&b_tx_ifaces, "{sa{sv}}", "org.bluez.GattCharacteristic1", &b_tx_props);
		g_variant_builder_add(&b_app, "{oa{sa{sv}}}", CHAR_TX_PATH, &b_tx_ifaces);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{oa{sa{sv}}})", &b_app));
    }
}

static void handle_char_method_call(GDBusConnection *conn,
                                    const gchar *sender,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *method_name,
                                    GVariant *parameters,
                                    GDBusMethodInvocation *invocation,
                                    gpointer user_data) {

    if (g_strcmp0(method_name, "ReadValue") == 0) {
        printf("[Pi D-Bus Log] 手機正在執行 ReadValue (讀取 TX 緩衝區)\n");
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
        for (size_t i = 0; i < strlen(tx_buffer); i++) {
            g_variant_builder_add(&builder, "y", (guchar)tx_buffer[i]);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ay)", &builder));
        return;
    }

    if (g_strcmp0(method_name, "WriteValue") == 0) {
        GVariant *byte_array;
        GVariantIter iter;
        guchar b;
        int idx = 0;

        g_variant_get(parameters, "(@ay@a{sv})", &byte_array, NULL);
        g_variant_iter_init(&iter, byte_array);
        while (g_variant_iter_next(&iter, "y", &b)) {
            if (idx < sizeof(rx_buffer) - 1) {
                rx_buffer[idx++] = b;
            }
        }
        rx_buffer[idx] = '\0';
        g_variant_unref(byte_array);

        while (idx > 0 && (rx_buffer[idx - 1] == '\n' || rx_buffer[idx - 1] == '\r')) {
            rx_buffer[--idx] = '\0';
        }

        printf("\n===========================================\n");
        printf("<- [Pi RX 收到手機指令]: \"%s\"\n", rx_buffer);
        printf("===========================================\n");
        g_dbus_method_invocation_return_value(invocation, NULL);

        process_ble_command(conn, rx_buffer);
        return;
    }

    if (g_strcmp0(method_name, "StartNotify") == 0) {
        printf("[Pi D-Bus Log] 手機開啟 Notify 通知成功 (StartNotify)\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "StopNotify") == 0) {
        printf("[Pi D-Bus Log] 手機關閉 Notify 通知 (StopNotify)\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
}

static GVariant *handle_get_property(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *property_name,
                                     GError **error,
                                     gpointer user_data) {

    if (g_strcmp0(property_name, "Value") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
        for (size_t i = 0; i < strlen(tx_buffer); i++) {
            g_variant_builder_add(&builder, "y", (guchar)tx_buffer[i]);
        }
        return g_variant_builder_end(&builder);
    }
    return NULL;
}

static const GDBusInterfaceVTable om_vtable = { handle_object_manager_method_call, NULL, NULL };
static const GDBusInterfaceVTable char_vtable = { handle_char_method_call, handle_get_property, NULL };

static const gchar om_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg type='a{oa{sa{sv}}}' name='object_paths_interfaces_and_properties' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static const gchar char_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='ReadValue'>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "      <arg type='ay' name='value' direction='out'/>"
    "    </method>"
    "    <method name='WriteValue'>"
    "      <arg type='ay' name='value' direction='in'/>"
    "      <arg type='a{sv}' name='options' direction='in'/>"
    "    </method>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.DBus.Properties'>"
    "    <method name='Get'>"
    "      <arg type='s' name='interface_name' direction='in'/>"
    "      <arg type='s' name='property_name' direction='in'/>"
    "      <arg type='v' name='value' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void on_register_app_cb(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), res, &error);

    if (error) {
        printf("[警報] GattManager 註冊失敗: %s\n", error->message);
        g_error_free(error);
    } else {
        printf("===========================================\n");
        printf("[成功] GATT Service (FFF0) 與 RX/TX 已成功註冊！\n");
        printf("===========================================\n");
        g_variant_unref(result);

        system("sudo btmgmt power on > /dev/null 2>&1");
        system("sudo btmgmt le on > /dev/null 2>&1");
        system("sudo btmgmt connectable on > /dev/null 2>&1");
        system("sudo btmgmt advertising on > /dev/null 2>&1");
        printf("[廣播] BLE 廣播中，請重啟手機藍牙連線測試。\n");
    }
}

int main(int argc, char *argv[]) {
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GError *error = NULL;

    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!conn) {
        fprintf(stderr, "無法連接至 System DBus: %s\n", error->message);
        return 1;
    }

    GDBusNodeInfo *om_node = g_dbus_node_info_new_for_xml(om_xml, NULL);
    g_dbus_connection_register_object(conn, APP_PATH, om_node->interfaces[0], &om_vtable, NULL, NULL, NULL);

    GDBusNodeInfo *char_node = g_dbus_node_info_new_for_xml(char_xml, NULL);
    g_dbus_connection_register_object(conn, CHAR_RX_PATH, char_node->interfaces[0], &char_vtable, NULL, NULL, NULL);
    g_dbus_connection_register_object(conn, CHAR_TX_PATH, char_node->interfaces[0], &char_vtable, NULL, NULL, NULL);

    printf("===========================================\n");
    printf("  ITRI C-BLE GATT 服務非同步註冊中... \n");
    printf("===========================================\n");

    g_dbus_connection_call(
        conn,
        BLUEZ_BUS_NAME,
        ADAPTER_PATH,
        GATT_MANAGER_IFACE,
        "RegisterApplication",
        g_variant_new("(oa{sv})", APP_PATH, NULL),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        on_register_app_cb,
        NULL
    );

    g_main_loop_run(loop);
    return 0;
}