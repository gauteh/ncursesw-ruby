/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 *   Changes: Pawel Wilk, 2013                                              *
 ****************************************************************************/
 
#if defined(HAVE_FORM_DRIVER_W_H) || defined(HAVE_NCURSESW_FORM_DRIVER_W_H)

#include "form_driver_w.h"
#include "compat.h"

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int form_driver_w(FORM * form,int  c)
|
|   Description   :  This is the workhorse of the forms system. It checks
|                    to determine whether the character c is a request or
|                    data. If it is a request, the form driver executes
|                    the request and returns the result. If it is data
|                    (printable character), it enters the data into the
|                    current position in the current field. If it is not
|                    recognized, the form driver assumes it is an application
|                    defined command and returns E_UNKNOWN_COMMAND.
|                    Application defined command should be defined relative
|                    to MAX_FORM_COMMAND, the maximum value of a request.
|
|   Return Values :  E_OK              - success
|                    E_SYSTEM_ERROR    - system error
|                    E_BAD_ARGUMENT    - an argument is incorrect
|                    E_NOT_POSTED      - form is not posted
|                    E_INVALID_FIELD   - field contents are invalid
|                    E_BAD_STATE       - called from inside a hook routine
|                    E_REQUEST_DENIED  - request failed
|                    E_NOT_CONNECTED   - no fields are connected to the form
|                    E_UNKNOWN_COMMAND - command not known
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
form_driver(FORM *form, int status, int c)
{
  const Binding_Info *BI = (Binding_Info *) 0;
  int res = E_UNKNOWN_COMMAND;

  T((T_CALLED("form_driver(%p,%d, %d)"), (void *)form, status, c));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (!(form->field))
    RETURN(E_NOT_CONNECTED);

  assert(form->page);

  if (c == FIRST_ACTIVE_MAGIC)
    {
      form->current = _nc_First_Active_Field(form);
      RETURN(E_OK);
    }

  assert(form->current &&
	 form->current->buf &&
	 (form->current->form == form)
    );

  if (form->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  if (!(form->status & _POSTED))
    RETURN(E_NOT_POSTED);

  if ((c >= MIN_FORM_COMMAND && c <= MAX_FORM_COMMAND) &&
      ((bindings[c - MIN_FORM_COMMAND].keycode & Key_Mask) == c))
    BI = &(bindings[c - MIN_FORM_COMMAND]);

  if (BI)
    {
      typedef int (*Generic_Method) (int (*const) (FORM *), FORM *);
      static const Generic_Method Generic_Methods[] =
      {
	Page_Navigation,	/* overloaded to call field&form hooks */
	Inter_Field_Navigation,	/* overloaded to call field hooks      */
	NULL,			/* Intra-Field is generic              */
	Vertical_Scrolling,	/* Overloaded to check multi-line      */
	Horizontal_Scrolling,	/* Overloaded to check single-line     */
	Field_Editing,		/* Overloaded to mark modification     */
	NULL,			/* Edit Mode is generic                */
	NULL,			/* Field Validation is generic         */
	NULL			/* Choice Request is generic           */
      };
      size_t nMethods = (sizeof(Generic_Methods) / sizeof(Generic_Methods[0]));
      size_t method = (BI->keycode >> ID_Shft) & 0xffff;	/* see ID_Mask */

      if ((method >= nMethods) || !(BI->cmd))
	res = E_SYSTEM_ERROR;
      else
	{
	  Generic_Method fct = Generic_Methods[method];

	  if (fct)
	    res = fct(BI->cmd, form);
	  else
	    res = (BI->cmd) (form);
	}
    }
  else if (KEY_MOUSE == c)
    {
      MEVENT event;
      WINDOW *win = form->win ? form->win : StdScreen(Get_Form_Screen(form));
      WINDOW *sub = form->sub ? form->sub : win;

      getmouse(&event);
      if ((event.bstate & (BUTTON1_CLICKED |
			   BUTTON1_DOUBLE_CLICKED |
			   BUTTON1_TRIPLE_CLICKED))
	  && wenclose(win, event.y, event.x))
	{			/* we react only if the click was in the userwin, that means
				 * inside the form display area or at the decoration window.
				 */
	  int ry = event.y, rx = event.x;	/* screen coordinates */

	  res = E_REQUEST_DENIED;
	  if (mouse_trafo(&ry, &rx, FALSE))
	    {			/* rx, ry are now "curses" coordinates */
	      if (ry < sub->_begy)
		{		/* we clicked above the display region; this is
				 * interpreted as "scroll up" request
				 */
		  if (event.bstate & BUTTON1_CLICKED)
		    res = form_driver(form, REQ_PREV_FIELD);
		  else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
		    res = form_driver(form, REQ_PREV_PAGE);
		  else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
		    res = form_driver(form, REQ_FIRST_FIELD);
		}
	      else if (ry > sub->_begy + sub->_maxy)
		{		/* we clicked below the display region; this is
				 * interpreted as "scroll down" request
				 */
		  if (event.bstate & BUTTON1_CLICKED)
		    res = form_driver(form, REQ_NEXT_FIELD);
		  else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
		    res = form_driver(form, REQ_NEXT_PAGE);
		  else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
		    res = form_driver(form, REQ_LAST_FIELD);
		}
	      else if (wenclose(sub, event.y, event.x))
		{		/* Inside the area we try to find the hit item */
		  int i;

		  ry = event.y;
		  rx = event.x;
		  if (wmouse_trafo(sub, &ry, &rx, FALSE))
		    {
		      int min_field = form->page[form->curpage].pmin;
		      int max_field = form->page[form->curpage].pmax;

		      for (i = min_field; i <= max_field; ++i)
			{
			  FIELD *field = form->field[i];

			  if (Field_Is_Selectable(field)
			      && Field_encloses(field, ry, rx) == E_OK)
			    {
			      res = _nc_Set_Current_Field(form, field);
			      if (res == E_OK)
				res = _nc_Position_Form_Cursor(form);
			      if (res == E_OK
				  && (event.bstate & BUTTON1_DOUBLE_CLICKED))
				res = E_UNKNOWN_COMMAND;
			      break;
			    }
			}
		    }
		}
	    }
	}
      else
	res = E_REQUEST_DENIED;
    }
  else if (status != KEY_CODE_YES)
        {
	        res = Data_Entry(form, c);
        }
    }
  _nc_Refresh_Current_Field(form);
  RETURN(res);
}

#endif
