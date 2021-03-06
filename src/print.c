/*-------------------------------------------------------------------------
 *
 * print.c
 *	  themes initialization
 *
 * Portions Copyright (c) 2017-2018 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/print.c
 *
 *-------------------------------------------------------------------------
 */

#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "pspg.h"
#include "unicode.h"

void
window_fill(int window_identifier,
			int srcy,
			int srcx,						/* offset to displayed data */
			int cursor_row,					/* row of row cursor */
			DataDesc *desc,
			ScrDesc *scrdesc,
			Options *opts)
{
	int			maxy, maxx;
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	int			srcy_bak = srcy;
	char		*free_row;
	WINDOW		*win;
	Theme		*t;
	bool		is_footer = window_identifier == WINDOW_FOOTER;
	bool		is_fix_rows = window_identifier == WINDOW_LUC || window_identifier == WINDOW_FIX_ROWS;
	int			positions[100][2];
	int			npositions;

	win = scrdesc->wins[window_identifier];
	t = &scrdesc->themes[window_identifier];

	/* when we want to detect expanded records titles */
	if (desc->is_expanded_mode)
	{
		scrdesc->first_rec_title_y = -1;
		scrdesc->last_rec_title_y = -1;
	}

	/* fast leaving */
	if (win == NULL)
		return;

	/* skip first x LineBuffers */
	while (srcy > 1000)
	{
		lnb = lnb->next;
		srcy -= 1000;
	}

	lnb_row = srcy;
	row = 0;

	getmaxyx(win, maxy, maxx);

	while (row < maxy )
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr;
		LineInfo   *lineinfo = NULL;
		bool		is_bookmark_row = false;
		bool		is_cursor_row = false;
		bool		is_found_row = false;
		bool		is_pattern_row = false;

		is_cursor_row = row == cursor_row;

		if (lnb_row == 1000)
		{
			lnb = lnb->next;
			lnb_row = 0;
		}

		if (lnb != NULL && lnb_row < lnb->nrows)
		{
			rowstr = lnb->rows[lnb_row];
			if (lnb->lineinfo != NULL)
				lineinfo = &lnb->lineinfo[lnb_row];
			else
				lineinfo = NULL;
			lnb_row += 1;
		}
		else
			rowstr = NULL;

		is_bookmark_row = (lineinfo != NULL && (lineinfo->mask & LINEINFO_BOOKMARK) != 0) ? true : false;

		if (!is_fix_rows && *scrdesc->searchterm != '\0' && lnb != NULL &&  rowstr != NULL
					  && !opts->no_highlight_search)
		{
			if (lineinfo == NULL)
			{
				int		i;

				lnb->lineinfo = malloc(1000 * sizeof(LineInfo));
				if (lnb->lineinfo == NULL)
				{
					endwin();
					fprintf(stderr, "out of memory");
					exit(1);
				}
				memset(lnb->lineinfo, 0, 1000 * sizeof(LineInfo));

				for (i = 0; i < lnb->nrows; i++)
					lnb->lineinfo[i].mask = LINEINFO_UNKNOWN;

				lineinfo = &lnb->lineinfo[lnb_row - 1];
			}

			if (lineinfo->mask & LINEINFO_UNKNOWN)
			{
				const char *str = rowstr;

				lineinfo->mask ^= LINEINFO_UNKNOWN;
				lineinfo->mask &= ~(LINEINFO_FOUNDSTR | LINEINFO_FOUNDSTR_MULTI);

				while (str != NULL)
				{
					/*
					 * When we would to ignore case or lower case (in this case, we know, so
					 * pattern has not any upper char, then we have to use slower case insensitive
					 * searching.
					 */
					if (opts->ignore_case || (opts->ignore_lower_case && !scrdesc->has_upperchr))
						str = utf8_nstrstr(str, scrdesc->searchterm);
					else if (opts->ignore_lower_case && scrdesc->has_upperchr)
						str = utf8_nstrstr_ignore_lower_case(str, scrdesc->searchterm);
					else
						/* we can use case sensitive searching (binary comparation) */
						str = strstr(str, scrdesc->searchterm);

					if (str != NULL)
					{
						if (lineinfo->mask & LINEINFO_FOUNDSTR)
						{
							/* When we detect multi occurrence, then stop searching */
							lineinfo->mask |= LINEINFO_FOUNDSTR_MULTI;
							break;
						}
						else
						{
							lineinfo->mask |= LINEINFO_FOUNDSTR;
							lineinfo->start_char = utf8len_start_stop(rowstr, str);
						}

						str += scrdesc->searchterm_size;
					}
				}
			}
		}

		is_pattern_row = (lineinfo != NULL && (lineinfo->mask & LINEINFO_FOUNDSTR) != 0) ? true : false;

		/* prepare position cache, when first occurrence is visible */
		if (lineinfo != NULL && (lineinfo->mask & LINEINFO_FOUNDSTR_MULTI) != 0 &&
			  srcx + maxx > lineinfo->start_char &&
			  *scrdesc->searchterm != '\0')
		{
			const char *str = rowstr;

			npositions = 0;

			while (str != NULL && npositions < 100)
			{
				if (opts->ignore_case || (opts->ignore_lower_case && !scrdesc->has_upperchr))
					str = utf8_nstrstr(str, scrdesc->searchterm);
				else if (opts->ignore_lower_case && scrdesc->has_upperchr)
					str = utf8_nstrstr_ignore_lower_case(str, scrdesc->searchterm);
				else
					/* we can use case sensitive searching (binary comparation) */
					str = strstr(str, scrdesc->searchterm);

				if (str != NULL)
				{
					positions[npositions][0] = utf8len_start_stop(rowstr, str);
					positions[npositions][1] = positions[npositions][0] + scrdesc->searchterm_char_size;

					/* don't search more if we are over visible part */
					if (positions[npositions][1] > srcx + maxx)
					{
						npositions += 1;
						break;
					}

					str += scrdesc->searchterm_size;
					npositions += 1;
				}
			}
		}

		if (is_bookmark_row)
		{
			if (!is_footer)
			{
				if (desc->border_type == 2)
					active_attr = is_cursor_row ? t->cursor_bookmark_attr : t->bookmark_line_attr;
				else
					active_attr = is_cursor_row ? t->cursor_bookmark_attr : t->bookmark_data_attr;
			}
			else
				active_attr = is_cursor_row ? t->cursor_bookmark_attr : t->bookmark_data_attr;
		}
		else if (is_pattern_row)
		{
			if (!is_footer)
			{
				if (desc->border_type == 2)
					active_attr = is_cursor_row ? t->cursor_line_attr : t->pattern_line_attr;
				else
					active_attr = is_cursor_row ? t->cursor_data_attr : t->pattern_data_attr;
			}
			else
				active_attr = is_cursor_row ? t->cursor_data_attr : t->pattern_data_attr;
		}
		else
		{
			if (!is_footer)
			{
				if (desc->border_type == 2)
					active_attr = is_cursor_row ? t->cursor_line_attr : t->line_attr;
				else
					active_attr = is_cursor_row ? t->cursor_data_attr : t->data_attr;
			}
			else
				active_attr = is_cursor_row ? t->cursor_data_attr : t->data_attr;
		}

		wattron(win, active_attr);

		wmove(win, row++, 0);

		if (rowstr != NULL)
		{
			int		i;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;
			int		left_spaces;							/* aux left spaces */

			is_found_row = scrdesc->found && scrdesc->found_row == effective_row;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
				if (is_expand_head)
				{
					if (scrdesc->first_rec_title_y == -1)
						scrdesc->first_rec_title_y = row - 1;
					else
						scrdesc->last_rec_title_y = row - 1;
				}
			}
			else
			{
				if (!is_footer)
				{
					fix_line_attr_style = effective_row == desc->border_top_row ||
												effective_row == desc->border_head_row ||
												effective_row == desc->border_bottom_row;
				}
				else
					fix_line_attr_style = false;

				is_expand_head = false;
			}

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;
			while(i > 0)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
				{
					i -= utf_dsplen(rowstr);
					rowstr += utf8charlen(*rowstr);
					if (i < 0)
						left_spaces = -i;
				}
				else
					break;
			}

			/* Fix too hungry cutting when some multichar char is removed */
			if (left_spaces > 0)
			{
				char   *p;
				int		aux_left_spaces = left_spaces;

				free_row = malloc(left_spaces + strlen(rowstr) + 1);
				p = free_row;
				while (aux_left_spaces-- > 0)
				{
					*p++ = ' ';
				}
				strcpy(p, rowstr);
				rowstr = free_row;
			}
			else
				free_row = NULL;

			ptr = rowstr;
			bytes = 0;

			/* find length of maxx characters */
			if (*ptr != '\0')
			{
				i = 0;
				while (i < maxx)
				{
					if (is_expand_head && !is_pattern_row && !is_bookmark_row)
					{
						int		pos = srcx + i;
						int		new_attr;

						if (is_cursor_row)
							new_attr = pos >= ei_min && pos <= ei_max ? t->cursor_expi_attr : t->cursor_line_attr;
						else
							new_attr = pos >= ei_min && pos <= ei_max ? t->expi_attr : t->line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								waddnstr(win, rowstr, bytes);
								rowstr += bytes;
								bytes = 0;
							}

							/* disable current style */
							wattroff(win, active_attr);

							/* active new style */
							active_attr = new_attr;
							wattron(win, active_attr);
						}
					}
					else if (!fix_line_attr_style)
					{
						int		htrpos = srcx + i;
						int		new_attr = active_attr;
						char	unicode_border[10];
						int		unicode_border_bytes = 0;
						char	column_format;

						column_format = desc->headline_transl != NULL ? desc->headline_transl[htrpos] : ' ';

						if (opts->force_uniborder && desc->linestyle == 'a')
						{
							if (*(rowstr + left_spaces + bytes) == '|' &&
									(column_format == 'L' || column_format == 'R' || column_format == 'I'))
							{
								strncpy(unicode_border, "\342\224\202", 3);  /* │ */
								unicode_border_bytes = 3;
							}
							else
								unicode_border_bytes = 0;
						}

						if (is_bookmark_row)
						{
							if (!is_cursor_row )
								new_attr = column_format == 'd' ? t->bookmark_data_attr : t->bookmark_line_attr;
							else
								new_attr = t->cursor_bookmark_attr;
						}
						else if (is_pattern_row && !is_cursor_row)
						{
							if (is_footer)
								new_attr = t->pattern_data_attr;
							else if (htrpos < desc->headline_char_size)
								new_attr = column_format == 'd' ? t->pattern_data_attr : t->pattern_line_attr;

							if (new_attr == t->pattern_data_attr && htrpos >= lineinfo->start_char)
							{
								if ((lineinfo->mask & LINEINFO_FOUNDSTR_MULTI) != 0)
								{
									int		i;

									for (i = 0; i < npositions; i++)
									{
										if (htrpos >= positions[i][0] && htrpos < positions[i][1])
										{
											new_attr = t->found_str_attr;
											break;
										}
									}
								}
								else
								{
									if (htrpos < lineinfo->start_char + scrdesc->searchterm_char_size)
										new_attr = t->found_str_attr;
								}
							}
						}
						else if (is_footer)
							new_attr = is_cursor_row ? t->cursor_data_attr : t->data_attr;
						else if (htrpos < desc->headline_char_size)
						{
							if (is_cursor_row )
								new_attr = column_format == 'd' ? t->cursor_data_attr : t->cursor_line_attr;
							else
								new_attr = column_format == 'd' ? t->data_attr : t->line_attr;
						}

						if (is_cursor_row)
						{
							if (is_found_row && htrpos >= scrdesc->found_start_x &&
									htrpos < scrdesc->found_start_x + scrdesc->searchterm_char_size)
								new_attr = new_attr | A_REVERSE;
							else if (is_pattern_row && htrpos >= lineinfo->start_char)
							{
								if ((lineinfo->mask & LINEINFO_FOUNDSTR_MULTI) != 0)
								{
									int		i;

									for (i = 0; i < npositions; i++)
									{
										if (htrpos >= positions[i][0] && htrpos < positions[i][1])
										{
											new_attr = t->cursor_pattern_attr;
											break;
										}
									}
								}
								else
								{
									if (htrpos < lineinfo->start_char + scrdesc->searchterm_char_size)
										new_attr = t->cursor_pattern_attr;
								}
							}
						}

						if (unicode_border_bytes > 0 && bytes > 0)
						{
							waddnstr(win, rowstr, bytes);
							rowstr += bytes;
							bytes = 0;
						}

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								waddnstr(win, rowstr, bytes);
								rowstr += bytes;
								bytes = 0;
							}

							/* disable current style */
							wattroff(win, active_attr);

							/* active new style */
							active_attr = new_attr;
							wattron(win, active_attr);
						}

						if (unicode_border_bytes > 0)
						{
							waddnstr(win, unicode_border, unicode_border_bytes);
							bytes = 0;
							rowstr += 1;
						}
					}
					else
					{
						if (!is_footer)
						{
							int		new_attr;

							if (is_cursor_row)
								new_attr = t->cursor_line_attr;
							else
								new_attr = t->line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									waddnstr(win, rowstr, bytes);
									rowstr += bytes;
									bytes = 0;
								}

								/* disable current style */
								wattroff(win, active_attr);

								/* active new style */
								active_attr = new_attr;
								wattron(win, active_attr);
							}
						}
					}

					if (*ptr != '\0')
					{
						int len  = utf8charlen(*ptr);
						i += utf_dsplen(ptr);
						ptr += len;
						/* suboptimal logic */
						if (i <= maxx && rowstr < ptr)
							bytes += len;
					}
					else
					{
						break;
					}
				} /* end while */
			}
			else if (is_cursor_row)
				/* in this case i is not valid, but it is necessary for cursor line printing */
				i = 1;

			if (bytes > 0)
			{
				if (!fix_line_attr_style || !(desc->linestyle == 'a' && opts->force_uniborder))
					waddnstr(win, rowstr, bytes);
				else
				{
					int		i = 0;

					while (bytes > 0)
					{
						int		htrpos = srcx + i;
						int		column_format = desc->headline_transl[htrpos];
						bool	is_top_row = effective_row == desc->border_top_row;
						bool	is_head_row = effective_row == desc->border_head_row;
						bool	is_bottom_row = effective_row == desc->border_bottom_row;

						if (column_format == 'd' && *rowstr == '-')
						{
							waddnstr(win, "\342\224\200", 3);
							rowstr += 1;
							bytes -= 1;
						}
						else if (column_format == 'L' && (*rowstr == '+' || *rowstr == '|'))
						{
							if (is_head_row)
								waddnstr(win, "\342\224\234", 3);
							else if (is_top_row)
								waddnstr(win, "\342\224\214", 3);
							else /* bottom row */
								waddnstr(win, "\342\224\224", 3);

							rowstr += 1;
							bytes -= 1;
						}
						else if (column_format == 'I' && *rowstr == '+')
						{

							if (is_head_row)
								waddnstr(win, "\342\224\274", 3);
							else if (is_top_row)
								waddnstr(win, "\342\224\254", 3);
							else /* bottom row */
								waddnstr(win, "\342\224\264", 3);

							rowstr += 1;
							bytes -= 1;
						}
						else if (column_format == 'R' && (*rowstr == '+' || *rowstr == '|'))
						{
							if (is_head_row)
								waddnstr(win, "\342\224\244", 3);
							else if (is_top_row)
								waddnstr(win, "\342\224\220", 3);
							else /* bottom row */
								waddnstr(win, "\342\224\230", 3);

							rowstr += 1;
							bytes -= 1;

						}
						else
						{
							int len  = utf8charlen(*rowstr);

							waddnstr(win, rowstr, len);
							rowstr +=len;
							bytes -= len;
						}

						i += 1;
					}
				}
			}

			/* clean other chars on line */
			if (i < maxx)
				wclrtoeol(win);

			/* draw cursor or bookmark line to screen end of line */
			if (i < maxx)
			{
				if (is_cursor_row && !is_bookmark_row)
					mvwchgat(win, row - 1, i, -1, 0, PAIR_NUMBER(t->cursor_data_attr), 0);
				else if (!is_cursor_row && is_bookmark_row)
					mvwchgat(win, row - 1, i, -1, 0, PAIR_NUMBER(t->bookmark_data_attr), 0);
				else if (is_cursor_row && is_bookmark_row)
					mvwchgat(win, row - 1, i, -1, t->cursor_bookmark_attr, PAIR_NUMBER(t->cursor_bookmark_attr), 0);
				else if (!is_cursor_row && is_pattern_row)
					mvwchgat(win, row - 1, i, -1, 0, PAIR_NUMBER(t->pattern_data_attr), 0);
			}

			if (free_row != NULL)
			{
				free(free_row);
				free_row = NULL;
			}
		}
		else
		{
			wclrtobot(win);
			break;
		}

		wattroff(win, active_attr);
	}
}

#ifdef COLORIZED_NO_ALTERNATE_SCREEN

static void
ansi_colors(int pairno, short int *fc, short int *bc)
{
	pair_content(pairno, fc, bc);
	*fc = *fc != -1 ? *fc + 30 : 39;
	*bc = *bc != -1 ? *bc + 40 : 49;
}

#endif

static char *
ansi_attr(attr_t attr)
{

#ifndef COLORIZED_NO_ALTERNATE_SCREEN

	return "";

#else

	static char result[20];
	int		pairno;
	short int fc, bc;

	pairno = PAIR_NUMBER(attr);
	ansi_colors(pairno, &fc, &bc);

	if ((attr & A_BOLD) != 0)
	{
		snprintf(result, 20, "\e[1;%d;%dm", fc, bc);
	}
	else
		snprintf(result, 20, "\e[0;%d;%dm", fc, bc);

	return result;

#endif

}

/*
 * Print data to primary screen without ncurses
 */
static void
draw_rectange(int offsety, int offsetx,			/* y, x offset on screen */
			int maxy, int maxx,				/* size of visible rectangle */
			int srcy, int srcx,				/* offset to displayed data */
			DataDesc *desc,
			attr_t data_attr,				/* colors for data (alphanums) */
			attr_t line_attr,				/* colors for borders */
			attr_t expi_attr,				/* colors for expanded headers */
			bool clreoln)					/* force clear to eoln */
{
	int			row;
	LineBuffer *lnb = &desc->rows;
	int			lnb_row;
	attr_t		active_attr;
	int			srcy_bak = srcy;

	/* skip first x LineBuffers */
	while (srcy > 1000)
	{
		lnb = lnb->next;
		srcy -= 1000;
	}

	lnb_row = srcy;
	row = 0;

	if (offsety)
		printf("\e[%dB", offsety);

	while (row < maxy)
	{
		int			bytes;
		char	   *ptr;
		char	   *rowstr;

		if (lnb_row == 1000)
		{
			lnb = lnb->next;
			lnb_row = 0;
		}

		if (lnb != NULL && lnb_row < lnb->nrows)
			rowstr = lnb->rows[lnb_row++];
		else
			rowstr = NULL;

		active_attr = line_attr;
		printf("%s", ansi_attr(active_attr));

		row += 1;

		if (rowstr != NULL)
		{
			int		i;
			int		effective_row = row + srcy_bak - 1;		/* row was incremented before, should be reduced */
			bool	fix_line_attr_style;
			bool	is_expand_head;
			int		ei_min, ei_max;
			int		left_spaces;
			char   *free_row;

			if (desc->is_expanded_mode)
			{
				fix_line_attr_style = effective_row >= desc->border_bottom_row;
				is_expand_head = is_expanded_header(rowstr, &ei_min, &ei_max);
			}
			else
			{
				fix_line_attr_style = effective_row == desc->border_top_row ||
											effective_row == desc->border_head_row ||
											effective_row >= desc->border_bottom_row;
				is_expand_head = false;
			}

			if (offsetx != 0)
				printf("\e[%dC", offsetx);

			/* skip first srcx chars */
			i = srcx;
			left_spaces = 0;
			while(i > 0)
			{
				if (*rowstr != '\0' && *rowstr != '\n')
				{
					i -= utf_dsplen(rowstr);
					rowstr += utf8charlen(*rowstr);
					if (i < 0)
						left_spaces = -i;
				}
				else
					break;
			}

			/* Fix too hungry cutting when some multichar char is removed */
			if (left_spaces > 0)
			{
				char *p;

				free_row = malloc(left_spaces + strlen(rowstr) + 1);
				p = free_row;
				while (left_spaces-- > 0)
				{
					*p++ = ' ';
				}
				strcpy(p, rowstr);
				rowstr = free_row;
			}
			else
				free_row = NULL;

			ptr = rowstr;
			bytes = 0;

			/* find length of maxx characters */
			if (*ptr != '\0' && *ptr != '\n')
			{
				i = 0;
				while (i < maxx)
				{
					if (is_expand_head)
					{
						int		pos = srcx + i;
						int		new_attr;

						new_attr = pos >= ei_min && pos <= ei_max ? expi_attr : line_attr;

						if (new_attr != active_attr)
						{
							if (bytes > 0)
							{
								printf("%.*s", bytes, rowstr);
								rowstr += bytes;
								bytes = 0;
							}

							/* active new style */
							active_attr = new_attr;
							printf("%s", ansi_attr(active_attr));
						}
					}
					else if (!fix_line_attr_style && desc->headline_transl != NULL)
					{
						int htrpos = srcx + i;

						if (htrpos < desc->headline_char_size)
						{
							int		new_attr;

							new_attr = desc->headline_transl[htrpos] == 'd' ? data_attr : line_attr;

							if (new_attr != active_attr)
							{
								if (bytes > 0)
								{
									//waddnstr(win, rowstr, bytes);
									printf("%.*s", bytes, rowstr);
									rowstr += bytes;
									bytes = 0;
								}

								/* active new style */
								active_attr = new_attr;
								printf("%s", ansi_attr(active_attr));
							}
						}
					}

					if (*ptr != '\0' && *ptr != '\n')
					{
						int len  = utf8charlen(*ptr);
						i += utf_dsplen(ptr);
						ptr += len;
						bytes += len;
					}
					else
						break;
				}
			}

			if (bytes > 0)
			{
				printf("%.*s", bytes, rowstr);
				if (clreoln)
					printf("\e[K");
				printf("\n");
			}

			if (free_row != NULL)
			{
				free(free_row);
				free_row = NULL;
			}
		}
		else
			break;
	}
}

void
draw_data(Options *opts, ScrDesc *scrdesc, DataDesc *desc,
		  int first_data_row, int first_row, int cursor_col,
		  int footer_cursor_col, int fix_rows_offset)
{
	struct winsize size;
	int		i;

	if (ioctl(0, TIOCGWINSZ, (char *) &size) >= 0)
	{

		for (i = 0; i < min_int(size.ws_row - 1 - scrdesc->top_bar_rows, desc->last_row + 1); i++)
			printf("\eD");

		/* Go wit cursor to up */
		printf("\e[%dA", min_int(size.ws_row - 1 - scrdesc->top_bar_rows, desc->last_row + 1));

		/* Save cursor */
		printf("\e[s");

		if (scrdesc->fix_cols_cols > 0)
		{
			draw_rectange(scrdesc->fix_rows_rows, 0,
						  scrdesc->rows_rows , scrdesc->fix_cols_cols,
						  first_data_row + first_row - fix_rows_offset, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);
		}
		if (scrdesc->fix_rows_rows > 0)
		{
			/* Go to saved position */
			printf("\e[u\e[s");

			draw_rectange(0, scrdesc->fix_cols_cols,
						  scrdesc->fix_rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  desc->title_rows + fix_rows_offset, scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  true);
		}

		if (scrdesc->fix_rows_rows > 0 && scrdesc->fix_cols_cols > 0)
		{
			/* Go to saved position */
			printf("\e[u\e[s");

			draw_rectange(0, 0,
						  scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  desc->title_rows + fix_rows_offset, 0,
						  desc,
						  COLOR_PAIR(4) | A_BOLD, 0, COLOR_PAIR(8) | A_BOLD,
						  false);
		}

		if (scrdesc->rows_rows > 0 )
		{
			/* Go to saved position */
			printf("\e[u\e[s");

			draw_rectange(scrdesc->fix_rows_rows, scrdesc->fix_cols_cols,
						  scrdesc->rows_rows, size.ws_col - scrdesc->fix_cols_cols,
						  first_data_row + first_row - fix_rows_offset, scrdesc->fix_cols_cols + cursor_col,
						  desc,
						  opts->theme == 2 ? 0 | A_BOLD : 0,
						  opts->theme == 2 && (desc->headline_transl == NULL) ? A_BOLD : 0,
						  COLOR_PAIR(8) | A_BOLD,
						  true);
		}

		if (w_footer(scrdesc) != NULL)
		{
			/* Go to saved position */
			printf("\e[u\e[s");

			draw_rectange(scrdesc->fix_rows_rows + scrdesc->rows_rows, 0,
						  scrdesc->footer_rows, scrdesc->maxx,
						  first_data_row + first_row + scrdesc->rows_rows - fix_rows_offset, footer_cursor_col,
						  desc,
						  COLOR_PAIR(9), 0, 0, true);
		}

		/* reset */
		printf("\e[0m\r");

	}
}
