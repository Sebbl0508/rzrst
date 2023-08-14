#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/netlink.h>
#include <linux/cache.h>
#include <linux/mutex.h>
#include <net/netlink.h>
#include <net/genetlink.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sebbl0508");
MODULE_DESCRIPTION("Sidetone driver for Razer USB sound card");

#define USB_VENDOR_ID_RAZER 0x1532
#define USB_DEVICE_ID_RAZER_USB_SOUND_CARD 0x0529

#define RAZER_USB_SOUND_CARD_VOL_BUF_SIZE 2
#define RAZER_USB_SOUND_CARD_STATE_BUF_SIZE 1


// Generic Netlink registration
enum rzr_cmd {
    RZR_CMD_SET_VOLUME_STATE,
    __RZR_CMD_MAX
};
#define RZR_CMD_MAX (__RZR_CMD_MAX - 1)

enum rzr_attr {
    RZR_ATTR_UNSPEC,
    RZR_ATTR_STATE,
    RZR_ATTR_VOLUME,
    __RZR_ATTR_LAST
};
#define RZR_ATTR_MAX (__RZR_ATTR_LAST - 1)

static int rzr_set_volume_state(struct sk_buff* skb, struct genl_info* info);

static const struct nla_policy rzr_attr_policy[RZR_ATTR_MAX + 1] = {
    [RZR_ATTR_STATE]    = { .type = NLA_U8 },
    [RZR_ATTR_VOLUME]   = { .type = NLA_U8 }
};

static const struct genl_ops genl_ops[] = {
        {
                .cmd    = RZR_CMD_SET_VOLUME_STATE,
                .doit   = rzr_set_volume_state,
        }
};

static struct genl_family razer_genl_family __ro_after_init = {
        .id             = 0x0, // let the kernel generate an id
        .ops            = genl_ops,
        .n_ops          = ARRAY_SIZE(genl_ops),
        .name           = "rzrst",
        .version        = 1,
        .maxattr        = RZR_ATTR_MAX,
        .module         = THIS_MODULE,
        .policy         = rzr_attr_policy,
        .netnsok        = true // ?
};


struct razer_usb_sound_card {
    struct hid_device* hdev;
    struct usb_device* usb_dev;
    struct urb* urb;

    struct usb_ctrlrequest* volume_req;
    struct usb_ctrlrequest* state_req;

    uint8_t* volume_packet_data;
    uint8_t* state_packet_data;
};


static struct razer_usb_sound_card* usb_sound_card = NULL;
/* THIS MUTEX ISN'T USED YET
 * (What about removing the USB device while a genl message is being processed?)
 * (What about changing volume/state?)
 * (What about sending genl commands in a for loop?)
 *   (-> the urb will be overwritten while it's in use :/)
 */
DEFINE_MUTEX(usb_sound_card_mtx);

static const struct usb_ctrlrequest volume_req = {
    .bRequestType = 0x21,
    .bRequest     = 1,
    .wValue       = cpu_to_le16(0x0200),
    .wIndex       = cpu_to_le16(0x0B00),
    .wLength      = cpu_to_le16(0x0002),
};

static const struct usb_ctrlrequest state_req = {
    .bRequestType = 0x21,
    .bRequest     = 1,
    .wValue       = cpu_to_le16(0x0100),
    .wIndex       = cpu_to_le16(0x0B00),
    .wLength      = cpu_to_le16(0x0001),
};

static uint8_t data_volume_100[2] = { 0x00, 0xEA };
static uint8_t data_set_on[1] = { 0x00 };

static void razer_state_res_handler(struct urb* urb) {
    struct hid_device* hdev = urb->context;

    // TODO: unlock mutex!

    if(urb->status)
        hid_err(hdev, "URB to set state failed with err %d\n", urb->status);
    else
        printk(KERN_INFO "rzrst: (?) successfully set sidetone on 100%%\n");
}

// handles response to sent volume packet
// -> on success sends the state packet
static void razer_vol_res_handler(struct urb* urb) {
    struct hid_device* hdev = urb->context;

    int ret;

    if(urb->status) {
        hid_err(hdev, "URB to set volume failed with err %d\n", urb->status);
        return;
    }

    // TODO: lock usb_sound_card mutex

    usb_fill_control_urb(
        usb_sound_card->urb,
        usb_sound_card->usb_dev,
        usb_sndctrlpipe(usb_sound_card->usb_dev, 0),
        (char*)usb_sound_card->state_req,
        usb_sound_card->state_packet_data,
        RAZER_USB_SOUND_CARD_STATE_BUF_SIZE,
        razer_state_res_handler,
        hdev
    );

    ret = usb_submit_urb(usb_sound_card->urb, GFP_ATOMIC);
    if(ret) {
        hid_err(hdev, "Error %d while submitting the set-state URB. Can't activate sidetone\n", ret);
        // TODO: unlock usb_sound_card mutex
    }
}

static void razer_send_usb_cmd(uint8_t volume, uint8_t state) {
    int ret = 0;

    usb_sound_card->state_packet_data[0] = state;

    usb_fill_control_urb(
        usb_sound_card->urb,
        usb_sound_card->usb_dev,
        usb_sndctrlpipe(usb_sound_card->usb_dev, 0),
        (char*)usb_sound_card->volume_req,
        usb_sound_card->volume_packet_data,
        RAZER_USB_SOUND_CARD_VOL_BUF_SIZE,
        razer_vol_res_handler,
        usb_sound_card->hdev
    );

    ret = usb_submit_urb(usb_sound_card->urb, GFP_ATOMIC);
    if(ret) {
        hid_err(usb_sound_card->hdev, "Error %d while submitting set-volume URB. can't change sidetone volume.\n", ret);
    }
}

// should active microphone sidetone with 100% volume on connection
static int razer_probe(struct hid_device* hdev, const struct hid_device_id* id) {
    printk(KERN_INFO "rzrst: Got device\n");

    // TODO: lock usb_sound_card mutex

    int ret = 0;

    if(!hid_is_usb(hdev)) {
        printk(KERN_WARNING "rzrst: Got a non-hid usb device\n");
        return -EINVAL;
    }

    ret = hid_parse(hdev);
    if(ret) {
        hid_err(hdev, "parse failed with err %d\n", ret);
        goto error0;
    }


    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_HIDRAW);
    if (ret) {
        hid_err(hdev, "hw start failed with err %d\n", ret);
        goto error0;
    }


    // allocate sound card struct
    usb_sound_card = kzalloc(sizeof(struct razer_usb_sound_card), GFP_KERNEL);
    if(!usb_sound_card) {
        ret = -ENOMEM;
        goto error1;
    }

    // allocate URB
    usb_sound_card->urb = usb_alloc_urb(0, GFP_ATOMIC);
    if(!usb_sound_card->urb) {
        ret = -ENOMEM;
        goto error2;
    }

    // allocate request buffers for volume...
    usb_sound_card->volume_req = kmemdup(&volume_req, sizeof(struct usb_ctrlrequest), GFP_KERNEL);
    if(!usb_sound_card->volume_req) {
        ret = -ENOMEM;
        goto error3;
    }

    // .. and state (ON/OFF)
    usb_sound_card->state_req = kmemdup(&state_req, sizeof(struct usb_ctrlrequest), GFP_KERNEL);
    if(!usb_sound_card->state_req) {
        ret = -ENOMEM;
        goto error4;
    }


    // create buffers for command packets
    usb_sound_card->volume_packet_data = kmemdup(
        data_volume_100,
        RAZER_USB_SOUND_CARD_VOL_BUF_SIZE,
        GFP_KERNEL
    );
    if(usb_sound_card->volume_packet_data == NULL) {
        ret = -ENOMEM;
        goto error5;
    }

    usb_sound_card->state_packet_data = kmemdup(
        data_set_on,
        RAZER_USB_SOUND_CARD_STATE_BUF_SIZE,
        GFP_KERNEL
    );
    if(usb_sound_card->state_packet_data == NULL) {
        ret = -ENOMEM;
        goto error6;
    }

    usb_sound_card->hdev    = hdev;
    usb_sound_card->usb_dev = interface_to_usbdev(to_usb_interface(hdev->dev.parent));


    // register generic netlink family
    ret = genl_register_family(&razer_genl_family);
    if(ret < 0) {
        printk(KERN_ERR "rzrst: error registering generic netlink family: %d\n", ret);
        goto error7;
    }

    return ret;

error7: kfree(usb_sound_card->state_packet_data);
error6: kfree(usb_sound_card->volume_packet_data);
error5: kfree(usb_sound_card->state_req);
error4: kfree(usb_sound_card->volume_req);
error3: usb_free_urb(usb_sound_card->urb);
error2: kfree(usb_sound_card);
error1: hid_hw_stop(hdev);
error0:
    return ret;
}

static void razer_remove(struct hid_device* hdev) {
    genl_unregister_family(&razer_genl_family);

    usb_kill_urb(usb_sound_card->urb);
    kfree(usb_sound_card->volume_req);
    kfree(usb_sound_card->state_req);
    kfree(usb_sound_card->state_packet_data);
    kfree(usb_sound_card->volume_packet_data);
    usb_free_urb(usb_sound_card->urb);
    kfree(usb_sound_card);

    hid_hw_stop(hdev);
}

static int rzr_set_volume_state(struct sk_buff* skb, struct genl_info* info) {
    uint8_t in_attr_volume  = nla_get_u8(info->attrs[RZR_ATTR_VOLUME]);
    uint8_t in_attr_state   = nla_get_u8(info->attrs[RZR_ATTR_STATE]);
    printk(KERN_INFO "rzrst: set volume %d state %d\n", in_attr_volume, in_attr_state);

    razer_send_usb_cmd(0, in_attr_state);

    return 0;
}

// USB Driver registration

static const struct hid_device_id razer_audio_devices[] = {
        { HID_USB_DEVICE(USB_VENDOR_ID_RAZER, USB_DEVICE_ID_RAZER_USB_SOUND_CARD) },
        { }
};
MODULE_DEVICE_TABLE(hid, razer_audio_devices);

static struct hid_driver razer_driver = {
        .name       = "rzrst",
        .id_table   = razer_audio_devices,
        .probe      = razer_probe,
        .remove     = razer_remove
};
module_hid_driver(razer_driver);