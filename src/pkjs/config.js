// Clay settings page. The two URL fields take the example links the Maker API
// app prints verbatim - base URL, app id and access token are read out of them,
// so nothing has to be assembled by hand. See NOTICE.

module.exports = [
  {
    type: "heading",
    defaultValue: "Hubitat Control"
  },
  {
    type: "text",
    defaultValue: "In your hub, open the Maker API app and copy the two example URLs it lists under \"Get All Devices\". Paste each one below exactly as shown, including the access token."
  },
  {
    type: "section",
    items: [
      {
        type: "heading",
        defaultValue: "Hub"
      },
      {
        type: "input",
        messageKey: "HubLocalUrl",
        defaultValue: "",
        label: "Local URL",
        description: "The one that starts http:// and contains your hub's IP address. Used on your home network.",
        attributes: {
          placeholder: "http://192.168.1.10/apps/api/34/devices?access_token=...",
          maxlength: 200
        }
      },
      {
        type: "input",
        messageKey: "HubCloudUrl",
        defaultValue: "",
        label: "Cloud URL",
        description: "The one that starts https://cloud.hubitat.com. Used when you are away from home.",
        attributes: {
          placeholder: "https://cloud.hubitat.com/api/.../apps/34/devices?access_token=...",
          maxlength: 240
        }
      },
      {
        type: "select",
        messageKey: "HubMode",
        defaultValue: "local",
        label: "Control Mode",
        description: "Local is faster and keeps traffic on your network. Cloud works anywhere your phone has data. The watch's own Connection row switches between them without coming back here.",
        options: [
          { label: "Local", value: "local" },
          { label: "Cloud", value: "cloud" }
        ]
      }
    ]
  },
  {
    type: "section",
    items: [
      {
        type: "heading",
        defaultValue: "Away From Home"
      },
      {
        type: "text",
        defaultValue: "Your access token is a key to every device on the hub. In Local mode it is sent to a private address like 192.168.1.10 — and on a public network that address may belong to somebody else's device. These settings decide what happens when you are not at home."
      },
      {
        type: "toggle",
        messageKey: "HubCloudAvailable",
        defaultValue: true,
        label: "Cloud Access Available",
        description: "Turn off if you did not enable cloud access in the Maker API app, or you would rather your hub was never reached over the internet. With this off the app is Local only and will simply stop when you are away."
      },
      {
        type: "toggle",
        messageKey: "HubVpn",
        defaultValue: false,
        label: "VPN To Home Network",
        description: "Turn on if you use a VPN back home. Local mode then works while you are away, provided the VPN is actually connected — the app checks the hub is reachable before it sends anything either way."
      },
      {
        type: "toggle",
        messageKey: "HubVerifyLocal",
        defaultValue: true,
        label: "Verify Hub Before Sending Token",
        description: "Strongly recommended. Checks the hub answers at that address using a request that carries no token, and only sends your credentials once it does. Turning this off sends the token to whatever occupies that address on the network you are joined to."
      }
    ]
  },
  {
    type: "section",
    items: [
      {
        type: "heading",
        defaultValue: "Locks And Doors"
      },
      {
        type: "text",
        defaultValue: "Holding the select button on the device list toggles a device without opening it. Locks, garage doors and door controls are left out of that shortcut, so unlocking or opening always means choosing the device first."
      },
      {
        type: "toggle",
        messageKey: "HubQuickUnlock",
        defaultValue: false,
        label: "Allow Locks And Doors In Shortcut",
        description: "WARNING: turning this on means one long press on a list row can unlock your front door or open your garage, with nothing else to confirm it. A press you did not intend has the same effect as one you did. Leave this off unless you have a specific reason and accept that risk."
      }
    ]
  },
  {
    type: "section",
    items: [
      {
        type: "heading",
        defaultValue: "Device List"
      },
      {
        type: "toggle",
        messageKey: "HubShowSensors",
        defaultValue: true,
        label: "Show Read-Only Devices",
        description: "Sensors, thermostats and meters appear with their current reading but take no commands. Turn this off for a shorter list of things you can actually control."
      },
      {
        type: "select",
        messageKey: "HubSort",
        defaultValue: "name",
        label: "Order",
        options: [
          { label: "By name", value: "name" },
          { label: "By type, then name", value: "kind" },
          { label: "Hub order", value: "hub" }
        ]
      },
      {
        type: "select",
        messageKey: "HubStep",
        defaultValue: "5",
        label: "Dimmer / Volume Step",
        description: "How far one press of up or down moves a level.",
        options: [
          { label: "1%", value: "1" },
          { label: "5%", value: "5" },
          { label: "10%", value: "10" },
          { label: "20%", value: "20" },
          { label: "25%", value: "25" }
        ]
      }
    ]
  },
  {
    type: "submit",
    defaultValue: "Save Settings"
  },
  {
    type: "text",
    defaultValue: "The device list is fetched once and kept until you choose Refresh Devices on the watch, change these settings, or restart the watch. Current states are read fresh every time the app opens."
  },
  {
    type: "text",
    defaultValue: "An unofficial community app, not affiliated with or endorsed by Hubitat, Inc. \"Hubitat\" is their trademark, used here only to say which hub this app talks to."
  }
];
