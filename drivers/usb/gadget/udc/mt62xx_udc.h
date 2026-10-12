#ifndef MT62XX_UDC_H
#define MT62XX_UDC_H


#define USB_FADDR           0x00
#define USB_POWER           0x01
#define USB_INTRIN          0x02
#define USB_INTROUT         0x04
#define USB_INTRUSB         0x06
#define USB_INTRINE         0x07
#define USB_INTROUTE        0x09 
#define USB_INTRUSBE        0x0B
#define USB_INDEX           0x0E
#define USB_RSTCTRL         0x0F

#define USB_INMAXP          0x10
#define USB_EP0_CSR         0x11
#define USB_INCSR1          0x11
#define USB_INCSR2          0x12
#define USB_OUTMAXP         0x13
#define USB_OUTCSR1         0x14
#define USB_OUTCSR2         0x15
#define USB_COUNT_LSB       0x16 
#define USB_COUNT_MSB       0x17

#define USB_FIFO(ep)        (0x20 + ((ep) * 4))

#define USB_CON             0x0240


/* USB_INTRUSB & USB_INTRUSBE */
#define INTRUSB_SUSP        BIT(0)
#define INTRUSB_RESUME      BIT(1)
#define INTRUSB_RESET       BIT(2)
#define INTRUSB_SOF         BIT(3)
#define INTRUSB_POWERDWN    BIT(4)

/* USB_EP0_CSR */
#define EP0_OUTPKTRDY       BIT(0)
#define EP0_INPKTRDY        BIT(1)
#define EP0_SENTSTALL       BIT(2)
#define EP0_DATAEND         BIT(3)
#define EP0_SETUPEND        BIT(4)
#define EP0_SENDSTALL       BIT(5)
#define EP0_SOUTPKTRDY      BIT(6) // Clear OUTPKTRDY
#define EP0_SSETUPEND       BIT(7) // Clear SETUPEND

/* USB_INCSR1 (IN Endpoints) */
#define INCSR1_INPKTRDY     BIT(0)
#define INCSR1_FIFONOTEMPTY BIT(1)
#define INCSR1_UNDERRUN     BIT(2)
#define INCSR1_FLUSHFIFO    BIT(3)
#define INCSR1_SENDSTALL    BIT(4)
#define INCSR1_SENTSTALL    BIT(5)
#define INCSR1_CLRDATATOG   BIT(6)

/* USB_OUTCSR1 (OUT Endpoints) */
#define OUTCSR1_OUTPKTRDY   BIT(0)
#define OUTCSR1_FIFOFULL    BIT(1)
#define OUTCSR1_OVERRUN     BIT(2)
#define OUTCSR1_DATAERROR   BIT(3)
#define OUTCSR1_FLUSHFIFO   BIT(4)
#define OUTCSR1_SENDSTALL   BIT(5)
#define OUTCSR1_SENTSTALL   BIT(6)
#define OUTCSR1_CLRDATATOG  BIT(7)

/* USB_INCSR2 (IN Endpoints) */
#define INCSR2_RFCDATATOG   BIT(3)
#define INCSR2_DMAENAB      BIT(4)
#define INCSR2_MODE         BIT(5)
#define INCSR2_ISO          BIT(6)
#define INCSR2_AUTOSET      BIT(7)

/* USB_CON */
#define USB_CON_DPPULLUP    BIT(0)
#define USB_CON_DMPULLUP    BIT(1)
#define USB_CON_PKT_FIX     BIT(5)

#define EP0                 0
#define EP1                 1
#define EP2                 2
#define EP3                 3
#define EP4                 4

#define INTR_EP(x)          BIT(x)
#define INTR_EP0            INTR_EP(EP0)
#define INTR_EP1            INTR_EP(EP1)
#define INTR_EP2            INTR_EP(EP2)
#define INTR_EP3            INTR_EP(EP3)
#define INTR_EP4            INTR_EP(EP4)

#define USB_ADDR_MASK       0x7F

struct mt62xx_ep {
	struct usb_ep       ep;
	struct mt62xx_udc   *udc;
	struct list_head    queue;
	u8                  epnum;
	bool                is_in;
	u16                 maxpacket;
};

struct mt62xx_udc {
	struct usb_gadget           gadget;
	struct usb_gadget_driver    *driver;
	void __iomem                *regs_base;
	struct usb_request          *ep0_req;
	spinlock_t                  lock;
	int                         irq;
	u32                         ep0_req_sent;
	u8                          pending_address;
	bool                        address_pending;
	bool                        ep0_dir_in;
	struct usb_ep               ep0;
	struct mt62xx_ep            ep_in[5];
	struct mt62xx_ep            ep_out[3];
};

#define ERR(stuff...)		pr_err("MT62xx USB: " stuff)
#define WARNING(stuff...)	pr_warn("MT62xx USB: " stuff)
#define INFO(stuff...)		pr_info("MT62xx USB: " stuff)
#define DBG(stuff...)		pr_debug("MT62xx USB: " stuff)

#endif // MT62XX_UDC_H