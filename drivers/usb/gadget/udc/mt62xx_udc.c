#undef	VERBOSE_DEBUG
#undef	PACKET_TRACE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/prefetch.h>
#include <linux/clk.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/atmel.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "mt62xx_udc.h"

static void mt62xx_nuke_queue(struct mt62xx_udc *udc, struct mt62xx_ep *mep)
{
    struct usb_request *req, *tmp;
    
    if (!mep || !mep->ep.name) return;

    list_for_each_entry_safe(req, tmp, &mep->queue, list) {
        list_del(&req->list);
        req->status = -ESHUTDOWN;
        spin_unlock(&udc->lock);
        if (req->complete) req->complete(&mep->ep, req);
        spin_lock(&udc->lock);
    }
}

static struct usb_request *mt62xx_alloc_request(struct usb_ep *ep, gfp_t gfp_flags)
{
	struct usb_request *req = kzalloc(sizeof(*req), gfp_flags);
	return req;
}

static void mt62xx_free_request(struct usb_ep *ep, struct usb_request *req)
{
	kfree(req);
}

static int mt62xx_ep_enable(struct usb_ep *ep, const struct usb_endpoint_descriptor *desc)
{
	struct mt62xx_ep *mep;
	struct mt62xx_udc *udc;
	unsigned long flags;
	u16 maxpacket;

	if (!ep || !desc || strcmp(ep->name, "ep0") == 0)
		return -EINVAL;

	mep = container_of(ep, struct mt62xx_ep, ep);
	udc = mep->udc;
	
	maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	pr_info("MT62xx USB: Enabling EP%d %s, maxpacket=%d\n", 
	        mep->epnum, mep->is_in ? "IN" : "OUT", maxpacket);

	spin_lock_irqsave(&udc->lock, flags);

	writeb(mep->epnum, udc->regs_base + USB_INDEX);

	if (mep->is_in) {
		writeb(maxpacket, udc->regs_base + USB_INMAXP);
		writeb(INCSR1_CLRDATATOG | INCSR1_FLUSHFIFO, udc->regs_base + USB_INCSR1);
		writeb(INCSR2_MODE, udc->regs_base + USB_INCSR2); 

		writeb(readb(udc->regs_base + USB_INTRINE) | INTR_EP(mep->epnum), udc->regs_base + USB_INTRINE);
	} else {
		writeb(maxpacket, udc->regs_base + USB_OUTMAXP);
		writeb(OUTCSR1_CLRDATATOG | OUTCSR1_FLUSHFIFO, udc->regs_base + USB_OUTCSR1);

		writeb(readb(udc->regs_base + USB_INTROUTE) | INTR_EP(mep->epnum), udc->regs_base + USB_INTROUTE);
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int mt62xx_ep_disable(struct usb_ep *ep)
{
	struct mt62xx_ep *mep;
	struct mt62xx_udc *udc;
	unsigned long flags;

	if (!ep || strcmp(ep->name, "ep0") == 0)
		return -EINVAL;

	mep = container_of(ep, struct mt62xx_ep, ep);
	udc = mep->udc;

	pr_info("MT62xx USB: Disabling EP%d %s\n", mep->epnum, mep->is_in ? "IN" : "OUT");

	spin_lock_irqsave(&udc->lock, flags);

	mt62xx_nuke_queue(udc, mep);

	writeb(mep->epnum, udc->regs_base + USB_INDEX);
	if (mep->is_in) {
		writeb(INCSR1_FLUSHFIFO, udc->regs_base + USB_INCSR1);

		writeb(readb(udc->regs_base + USB_INTRINE) & ~INTR_EP(mep->epnum), udc->regs_base + USB_INTRINE);
	} else {
		writeb(OUTCSR1_FLUSHFIFO, udc->regs_base + USB_OUTCSR1);

		writeb(readb(udc->regs_base + USB_INTROUTE) & ~INTR_EP(mep->epnum), udc->regs_base + USB_INTROUTE);
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int mt62xx_ep_queue(struct usb_ep *ep, struct usb_request *req, gfp_t gfp_flags)
{
	struct mt62xx_udc *udc;
	unsigned long flags;
	u8 *buf = (u8 *)req->buf;
	int i;

	req->actual = 0;
	req->status = -EINPROGRESS;

	if (strcmp(ep->name, "ep0") == 0) {
		udc = container_of(ep, struct mt62xx_udc, ep0);

		spin_lock_irqsave(&udc->lock, flags);

		udc->ep0_req = req;
		udc->ep0_req_sent = 0;
		
		writeb(0, udc->regs_base + USB_INDEX);

		if (udc->ep0_dir_in) {
			int to_send = (req->length > 64) ? 64 : req->length;
			for (i = 0; i < to_send; i++) {
				writeb(buf[i], udc->regs_base + USB_FIFO(0));
			}
			udc->ep0_req_sent += to_send;
			
			u8 next_csr = EP0_INPKTRDY;
			if (udc->ep0_req_sent == req->length) next_csr |= EP0_DATAEND;
			writeb(next_csr, udc->regs_base + USB_EP0_CSR);

			spin_unlock_irqrestore(&udc->lock, flags);
		} else {
			if (req->length == 0) {
				writeb(EP0_INPKTRDY | EP0_DATAEND, udc->regs_base + USB_EP0_CSR);
				udc->ep0_req = NULL;
				req->status = 0;
				spin_unlock_irqrestore(&udc->lock, flags);
				if (req->complete) req->complete(ep, req);
			} else {
				spin_unlock_irqrestore(&udc->lock, flags);
			}
		}
		return 0;
	}

	struct mt62xx_ep *mep = container_of(ep, struct mt62xx_ep, ep);
	udc = mep->udc;

	spin_lock_irqsave(&udc->lock, flags);

	if (mep->is_in) {
		bool is_empty = list_empty(&mep->queue);
		list_add_tail(&req->list, &mep->queue);
		
		if (is_empty) {
			writeb(mep->epnum, udc->regs_base + USB_INDEX);
			int to_send = (req->length > mep->maxpacket) ? mep->maxpacket : req->length;
			
			for (i = 0; i < to_send; i++) 
				writeb(buf[i], udc->regs_base + USB_FIFO(mep->epnum));
				
			req->actual += to_send;
			writeb(INCSR1_INPKTRDY, udc->regs_base + USB_INCSR1);
		}
	} else {
		list_add_tail(&req->list, &mep->queue);
		
		writeb(mep->epnum, udc->regs_base + USB_INDEX);
		if (readb(udc->regs_base + USB_OUTCSR1) & OUTCSR1_OUTPKTRDY) {
			u8 count = readb(udc->regs_base + USB_COUNT_LSB);
			u16 can_read = count;
			
			if (req->actual + can_read > req->length) 
				can_read = req->length - req->actual;
			
			for (i = 0; i < can_read; i++) 
				buf[req->actual + i] = readb(udc->regs_base + USB_FIFO(mep->epnum));
			req->actual += can_read;
			
			for (; i < count; i++) 
				readb(udc->regs_base + USB_FIFO(mep->epnum));
			
			writeb(0, udc->regs_base + USB_OUTCSR1);
			
			if (req->actual >= req->length || count < mep->maxpacket) {
				list_del(&req->list);
				req->status = 0;
				
				spin_unlock_irqrestore(&udc->lock, flags);
				if (req->complete) req->complete(ep, req);
				return 0;
			}
		}
	}
	
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0; 
}

static int mt62xx_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct mt62xx_udc *udc;
	unsigned long flags;
	bool found = false;

	if (strcmp(ep->name, "ep0") == 0) {
		udc = container_of(ep, struct mt62xx_udc, ep0);
		spin_lock_irqsave(&udc->lock, flags);
		
		if (udc->ep0_req == req) {
			udc->ep0_req = NULL;
			found = true;
			writeb(0, udc->regs_base + USB_INDEX);
			if (udc->ep0_dir_in) 
				writeb(INCSR1_FLUSHFIFO, udc->regs_base + USB_EP0_CSR);
		}
	} 
	else {
		struct mt62xx_ep *mep = container_of(ep, struct mt62xx_ep, ep);
		struct usb_request *iter, *tmp;
		udc = mep->udc;
		
		spin_lock_irqsave(&udc->lock, flags);

		list_for_each_entry_safe(iter, tmp, &mep->queue, list) {
			if (iter == req) {
				list_del(&iter->list);
				found = true;
				
				writeb(mep->epnum, udc->regs_base + USB_INDEX);
				if (mep->is_in) {
					writeb(INCSR1_FLUSHFIFO, udc->regs_base + USB_INCSR1);
				} else {
					writeb(OUTCSR1_FLUSHFIFO, udc->regs_base + USB_OUTCSR1);
				}
				break;
			}
		}
	}

	if (found) {
		req->status = -ECONNRESET;
		
		spin_unlock_irqrestore(&udc->lock, flags);
		if (req->complete) req->complete(ep, req);
		
		pr_debug("MT62xx USB: Dequeued request from %s\n", ep->name);
		return 0;
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return -EINVAL;
}

static int mt62xx_ep_set_halt(struct usb_ep *ep, int value)
{
	struct mt62xx_udc *udc;
	struct mt62xx_ep *mep;
	unsigned long flags;

	if (strcmp(ep->name, "ep0") == 0) 
		return -EINVAL; 

	mep = container_of(ep, struct mt62xx_ep, ep);
	udc = mep->udc;

	spin_lock_irqsave(&udc->lock, flags);

	if (value && !list_empty(&mep->queue)) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EAGAIN;
	}

	writeb(mep->epnum, udc->regs_base + USB_INDEX);

	if (mep->is_in) {
		u8 csr = readb(udc->regs_base + USB_INCSR1);
		if (value) {
			csr |= INCSR1_SENDSTALL;
		} else {
			csr &= ~(INCSR1_SENDSTALL | INCSR1_SENTSTALL);
			csr |= INCSR1_CLRDATATOG;
		}
		writeb(csr, udc->regs_base + USB_INCSR1);
	} else {
		u8 csr = readb(udc->regs_base + USB_OUTCSR1);
		if (value) {
			csr |= OUTCSR1_SENDSTALL;
		} else {
			csr &= ~(OUTCSR1_SENDSTALL | OUTCSR1_SENTSTALL);
			csr |= OUTCSR1_CLRDATATOG;
		}
		writeb(csr, udc->regs_base + USB_OUTCSR1);
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	
	pr_info("MT62xx USB: EP%d %s HALT %s\n", mep->epnum, mep->is_in ? "IN" : "OUT", value ? "set" : "cleared");
	return 0;
}

static const struct usb_ep_ops mt62xx_ep_ops = {
	.alloc_request = mt62xx_alloc_request,
	.free_request  = mt62xx_free_request,
	.enable        = mt62xx_ep_enable,
	.disable       = mt62xx_ep_disable,
	.queue         = mt62xx_ep_queue,
	.dequeue       = mt62xx_ep_dequeue,
	.set_halt      = mt62xx_ep_set_halt,
};

static void mt62xx_handle_in(struct mt62xx_udc *udc, struct mt62xx_ep *mep)
{
    if (list_empty(&mep->queue)) return;

    struct usb_request *req = list_first_entry(&mep->queue, struct usb_request, list);
    
    if (req->actual < req->length) {
        int remaining = req->length - req->actual;
        int to_send = (remaining > mep->maxpacket) ? mep->maxpacket : remaining;
        int i;
        u8 *buf = (u8 *)req->buf;
        
        writeb(mep->epnum, udc->regs_base + USB_INDEX);
        for (i = 0; i < to_send; i++) 
            writeb(buf[req->actual + i], udc->regs_base + USB_FIFO(mep->epnum));
        req->actual += to_send;
        writeb(INCSR1_INPKTRDY, udc->regs_base + USB_INCSR1);
    } else {
        list_del(&req->list);
        
        if (!list_empty(&mep->queue)) {
            struct usb_request *next_req = list_first_entry(&mep->queue, struct usb_request, list);
            int to_send = (next_req->length > mep->maxpacket) ? mep->maxpacket : next_req->length;
            int i;
            u8 *buf = (u8 *)next_req->buf;
            
            writeb(mep->epnum, udc->regs_base + USB_INDEX);
            for (i = 0; i < to_send; i++) 
                writeb(buf[i], udc->regs_base + USB_FIFO(mep->epnum));
            next_req->actual += to_send;
            writeb(INCSR1_INPKTRDY, udc->regs_base + USB_INCSR1);
        }
        
        req->status = 0;
        spin_unlock(&udc->lock);
        if (req->complete) req->complete(&mep->ep, req);
        spin_lock(&udc->lock);
    }
}

static void mt62xx_handle_out(struct mt62xx_udc *udc, struct mt62xx_ep *mep)
{
    writeb(mep->epnum, udc->regs_base + USB_INDEX);
    u8 outcsr1 = readb(udc->regs_base + USB_OUTCSR1);
    
    if (outcsr1 & OUTCSR1_OUTPKTRDY) { 
        u8 count = readb(udc->regs_base + USB_COUNT_LSB); 
        
        if (!list_empty(&mep->queue)) {
            struct usb_request *req = list_first_entry(&mep->queue, struct usb_request, list);
            u8 *buf = (u8 *)req->buf;
            int i;
            u16 can_read = count;
            
            if (req->actual + can_read > req->length) 
                can_read = req->length - req->actual;
            
            for (i = 0; i < can_read; i++) 
                buf[req->actual + i] = readb(udc->regs_base + USB_FIFO(mep->epnum));
            req->actual += can_read;
            
            for (; i < count; i++) 
                readb(udc->regs_base + USB_FIFO(mep->epnum));
            
            writeb(0, udc->regs_base + USB_OUTCSR1);
            
            if (req->actual >= req->length || count < mep->maxpacket) {
                list_del(&req->list);
                req->status = 0;
                
                spin_unlock(&udc->lock);
                if (req->complete) req->complete(&mep->ep, req);
                spin_lock(&udc->lock);
            }
        }
    }
}

static irqreturn_t mt62xx_udc_irq(int irq, void *dev_id)
{
    struct mt62xx_udc *udc = dev_id;
    u8 irq_usb, irq_in, irq_out;

    irq_usb = readb(udc->regs_base + USB_INTRUSB);
    irq_in  = readb(udc->regs_base + USB_INTRIN);
    irq_out = readb(udc->regs_base + USB_INTROUT);

    if (!irq_usb && !irq_in && !irq_out)
        return IRQ_NONE;

    spin_lock(&udc->lock);
    
    if (irq_usb & INTRUSB_RESET) {
        pr_info("MT62xx USB: Bus Reset detected!\n");
        int i;
        
        writeb(0, udc->regs_base + USB_FADDR);
        udc->address_pending = false;
        udc->ep0_dir_in = false;
        
		writeb(0, udc->regs_base + USB_INDEX);
        writeb(EP0_SSETUPEND | EP0_SOUTPKTRDY, udc->regs_base + USB_EP0_CSR);

        if (udc->ep0_req) {
            udc->ep0_req->status = -ESHUTDOWN;
            spin_unlock(&udc->lock);
            if (udc->ep0_req->complete) udc->ep0_req->complete(&udc->ep0, udc->ep0_req);
            spin_lock(&udc->lock);
            udc->ep0_req = NULL;
        }
        
        for (i = 1; i <= 4; i++) {
            mt62xx_nuke_queue(udc, &udc->ep_in[i]);
            writeb(i, udc->regs_base + USB_INDEX);
            writeb(INCSR1_CLRDATATOG | INCSR1_FLUSHFIFO, udc->regs_base + USB_INCSR1);
        }
        
        for (i = 1; i <= 2; i++) {
            mt62xx_nuke_queue(udc, &udc->ep_out[i]);
            writeb(i, udc->regs_base + USB_INDEX);
            writeb(OUTCSR1_CLRDATATOG | OUTCSR1_FLUSHFIFO, udc->regs_base + USB_OUTCSR1);
        }
        
        writeb(INTR_EP0, udc->regs_base + USB_INTRINE);
        writeb(0, udc->regs_base + USB_INTROUTE);
        
        if (udc->driver && udc->driver->reset) {
            spin_unlock(&udc->lock);
            udc->driver->reset(&udc->gadget);
            spin_lock(&udc->lock);
        }
    }

    if (irq_in & INTR_EP0) {
        u8 csr0;
        writeb(0, udc->regs_base + USB_INDEX);
        csr0 = readb(udc->regs_base + USB_EP0_CSR);
        
        if (csr0 == 0x00 && udc->address_pending) {
            writeb(udc->pending_address, udc->regs_base + USB_FADDR);
            udc->address_pending = false;
        }

        if (csr0 & EP0_SETUPEND) {
            writeb(EP0_SSETUPEND, udc->regs_base + USB_EP0_CSR);
            udc->ep0_req = NULL;
        }

        if (csr0 & EP0_OUTPKTRDY) {
            u8 count = readb(udc->regs_base + USB_COUNT_LSB); 
            
            if (udc->ep0_req && !udc->ep0_dir_in) {
                struct usb_request *req = udc->ep0_req;
                u8 *buf = (u8 *)req->buf;
                int i;
                u16 can_read = count;

                if (req->actual + can_read > req->length)
                    can_read = req->length - req->actual;
                
                for (i = 0; i < can_read; i++) 
                    buf[req->actual + i] = readb(udc->regs_base + USB_FIFO(0));
                req->actual += can_read;
                
                for (; i < count; i++) 
                    readb(udc->regs_base + USB_FIFO(0));
                
                u8 next_csr = EP0_SOUTPKTRDY;
                if (req->actual >= req->length) {
                    next_csr |= EP0_DATAEND; 
                    writeb(next_csr, udc->regs_base + USB_EP0_CSR);
                    
                    udc->ep0_req = NULL;
                    req->status = 0;
                    if (req->complete) req->complete(&udc->ep0, req);
                } else {
                    writeb(next_csr, udc->regs_base + USB_EP0_CSR);
                }
            }
            else if (count == 8) {
                struct usb_ctrlrequest setup;
                u8 *s_buf = (u8 *)&setup;
                int i, ret;
                
                udc->ep0_req = NULL;
                for (i = 0; i < 8; i++) 
                    s_buf[i] = readb(udc->regs_base + USB_FIFO(0));

                udc->ep0_dir_in = (setup.bRequestType & USB_DIR_IN) != 0;

                if (setup.bRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) && 
                    setup.bRequest == USB_REQ_SET_ADDRESS) {
                    udc->pending_address = le16_to_cpu(setup.wValue) & USB_ADDR_MASK;
                    udc->address_pending = true;
                    writeb(EP0_SOUTPKTRDY | EP0_DATAEND, udc->regs_base + USB_EP0_CSR);
                    return IRQ_HANDLED; 
                }

                writeb(EP0_SOUTPKTRDY, udc->regs_base + USB_EP0_CSR);

                if (udc->driver && udc->driver->setup) {
                    ret = udc->driver->setup(&udc->gadget, &setup);
                    if (ret < 0) {
                        writeb(EP0_SENDSTALL, udc->regs_base + USB_EP0_CSR);
                    } else if (setup.wLength == 0) {
                        writeb(EP0_INPKTRDY | EP0_DATAEND, udc->regs_base + USB_EP0_CSR);
                    }
                }
            } 
            else if (count == 0) {
                writeb(EP0_SOUTPKTRDY | EP0_DATAEND, udc->regs_base + USB_EP0_CSR);
            }
        }
        else if (udc->ep0_req) {
            struct usb_request *req = udc->ep0_req;
            
            if (udc->ep0_req_sent < req->length) {
                int remaining = req->length - udc->ep0_req_sent;
                int to_send = (remaining > 64) ? 64 : remaining;
                int i;
                u8 *buf = req->buf + udc->ep0_req_sent;
                u8 next_csr = EP0_INPKTRDY;
                
                for (i = 0; i < to_send; i++) 
                    writeb(buf[i], udc->regs_base + USB_FIFO(0));
                
                udc->ep0_req_sent += to_send;
                if (udc->ep0_req_sent == req->length) next_csr |= EP0_DATAEND;
                
                writeb(next_csr, udc->regs_base + USB_EP0_CSR);
            } 
            else {
                udc->ep0_req = NULL;
                req->status = 0;
                req->actual = req->length;
                if (req->complete) req->complete(&udc->ep0, req);
            }
        }
    }

    if (irq_in & INTR_EP1) mt62xx_handle_in(udc, &udc->ep_in[1]);
    if (irq_in & INTR_EP2) mt62xx_handle_in(udc, &udc->ep_in[2]);
    if (irq_in & INTR_EP3) mt62xx_handle_in(udc, &udc->ep_in[3]);
    if (irq_in & INTR_EP4) mt62xx_handle_in(udc, &udc->ep_in[4]);

    if (irq_out & INTR_EP1) mt62xx_handle_out(udc, &udc->ep_out[1]);
    if (irq_out & INTR_EP2) mt62xx_handle_out(udc, &udc->ep_out[2]);

    spin_unlock(&udc->lock);
    return IRQ_HANDLED;
}

static int mt62xx_pullup(struct usb_gadget *gadget, int is_on)
{
	struct mt62xx_udc *udc = container_of(gadget, struct mt62xx_udc, gadget);
	u8 val = readb(udc->regs_base + USB_CON);

	if (is_on)
		val |= USB_CON_DPPULLUP;
	else
		val &= ~USB_CON_DPPULLUP;

	writeb(val, udc->regs_base + USB_CON);
	INFO("Pullup %s\n", is_on ? "ON" : "OFF");
	return 0;
}

static int mt62xx_udc_start(struct usb_gadget *gadget,
			    struct usb_gadget_driver *driver)
{
	struct mt62xx_udc *udc = container_of(gadget, struct mt62xx_udc, gadget);
	udc->driver = driver;
	return 0;
}

static int mt62xx_udc_stop(struct usb_gadget *gadget)
{
	struct mt62xx_udc *udc = container_of(gadget, struct mt62xx_udc, gadget);
	udc->driver = NULL;
	return 0;
}

static const struct usb_gadget_ops mt62xx_gadget_ops = {
	.udc_start	= mt62xx_udc_start,
	.udc_stop	= mt62xx_udc_stop,
	.pullup		= mt62xx_pullup,
};

static void mt62xx_init_ep(struct mt62xx_udc *udc, struct mt62xx_ep *mep, 
                           const char *name, u8 epnum, bool is_in, 
                           u16 maxpacket, bool is_bulk) 
{
	mep->udc = udc;
	mep->epnum = epnum;
	mep->is_in = is_in;
	mep->maxpacket = maxpacket;
	INIT_LIST_HEAD(&mep->queue);

	mep->ep.name = name;
	mep->ep.ops = &mt62xx_ep_ops;
	
	if (is_bulk) mep->ep.caps.type_bulk = true;
	else mep->ep.caps.type_int = true;
	
	if (is_in) mep->ep.caps.dir_in = true;
	else mep->ep.caps.dir_out = true;

	usb_ep_set_maxpacket_limit(&mep->ep, maxpacket);
	list_add_tail(&mep->ep.ep_list, &udc->gadget.ep_list);
}

static int mt62xxudc_probe(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct mt62xx_udc *udc;
	struct resource *res;
	int ret;

	INFO("Probing UDC...\n");

	udc = devm_kzalloc(&pdev->dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	udc->regs_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(udc->regs_base)) {
		ERR("Failed to map memory\n");
		return PTR_ERR(udc->regs_base);
	}

	udc->irq = platform_get_irq(pdev, 0);
	if (udc->irq < 0)
		return udc->irq;

	ret = devm_request_irq(&pdev->dev, udc->irq, mt62xx_udc_irq,
			       0, "mt62xx_usb", udc);
	if (ret) {
		ERR("Failed to request IRQ\n");
		return ret;
	}

	spin_lock_init(&udc->lock);

	udc->gadget.ops = &mt62xx_gadget_ops;
	udc->gadget.max_speed = USB_SPEED_FULL;
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.name = "mt62xx-udc";

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	udc->ep0.name = "ep0";
	udc->ep0.ops = &mt62xx_ep_ops;
	udc->ep0.caps.type_control = true;
	udc->ep0.caps.dir_in = true;
	udc->ep0.caps.dir_out = true;
	usb_ep_set_maxpacket_limit(&udc->ep0, 64);
	udc->gadget.ep0 = &udc->ep0;
	
	mt62xx_init_ep(udc, &udc->ep_in[1],  "ep1in",     EP1, true,  64, true);
	mt62xx_init_ep(udc, &udc->ep_out[1], "ep1out",    EP1, false, 64, true);
	mt62xx_init_ep(udc, &udc->ep_in[2],  "ep2in",     EP2, true,  64, true);
	mt62xx_init_ep(udc, &udc->ep_out[2], "ep2out",    EP2, false, 64, true);
	mt62xx_init_ep(udc, &udc->ep_in[3],  "ep3in-int", EP3, true,  16, false);
	mt62xx_init_ep(udc, &udc->ep_in[4],  "ep4in-int", EP4, true,  16, false);
	
	writeb(INTRUSB_RESET, udc->regs_base + USB_INTRUSBE);

	writeb(INTR_EP0, udc->regs_base + USB_INTRINE);
	writeb(0, udc->regs_base + USB_INTROUTE);

	ret = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (ret)
		ERR("Failed to register UDC\n");
	else
		INFO("UDC registered successfully!\n");

	mt62xx_pullup(&udc->gadget, 0);
	
	msleep(200);
	
	mt62xx_pullup(&udc->gadget, 1);
	return ret;
}

static int mt62xxudc_remove(struct platform_device *pdev) 
{
	DBG("remove\n");
	struct mt62xx_udc *udc = platform_get_drvdata(pdev);
	usb_del_gadget_udc(&udc->gadget);
	return 0;
}

static void mt62xxudc_shutdown(struct platform_device *dev)
{
	DBG("shutdown\n");
}

#define	mt62xxudc_suspend	NULL
#define	mt62xxudc_resume	NULL

static struct platform_driver mt62xx_udc_driver = {
	.probe 		= mt62xxudc_probe,
	.remove		= mt62xxudc_remove,
	//.shutdown	= mt62xxudc_shutdown,
	//.suspend	= mt62xxudc_suspend,
	//.resume		= mt62xxudc_resume,
	.driver		= {
		.name	= "mt62xx-udc",
	},
};

module_platform_driver(mt62xx_udc_driver);

MODULE_LICENSE("GPL");
