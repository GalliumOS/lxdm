#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <string.h> // strcmp

void numlock(Display *c,int onoff)
{
	XkbDescPtr xkb;
	unsigned int mask;
	int i;
	xkb = XkbGetKeyboard(c, XkbAllComponentsMask, XkbUseCoreKbd );
	if(!xkb) return;
	if(!xkb->names)
	{
		XkbFreeKeyboard(xkb,0,True);
		return;
	}
	for(i = 0; i < XkbNumVirtualMods; i++)
	{
		char *s=XGetAtomName( xkb->dpy, xkb->names->vmods[i]);
		if(!s) continue;
		if(strcmp(s,"NumLock")) continue;
		XkbVirtualModsToReal( xkb, 1 << i, &mask );
		break;
	}
	XkbFreeKeyboard( xkb, 0, True );
	XkbLockModifiers ( c, XkbUseCoreKbd, mask, (onoff?mask:0));
}

int main(int arc,char *arg[])
{
	Display *c;
	if(arc!=2)
		return -1;
	c=XOpenDisplay(0);
	if(!c)
		return -1;
	if(!strcmp(arg[1],"on") || !strcmp(arg[1],"1"))
		numlock(c,1);
	else
		numlock(c,0);
	XCloseDisplay(c);
	return 0;
}

