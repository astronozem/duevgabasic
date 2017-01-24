#include <stdio.h>	 
#include <stdlib.h>	  
#include "string.h"
#include "console.h"
#include "bprog.h"
#include "routines.h"
#include "VGA_.h"
#include "fs.h"


typedef char *pchar;

#define   MAX_GOSUB_STACK     32
#define   MAX_FOR_LOOPS       16
#define   MAX_VARIABLES       96
#define   MAX_LINE_COUNT      5055
#define   MAX_INPUT_LENGTH    180

int bytes;
int *gosub_stack_ptr;
int	E[MAX_GOSUB_STACK];
int	L[MAX_FOR_LOOPS];
int	M[MAX_FOR_LOOPS]; 
int	var[MAX_VARIABLES]; 
int	lineNum, i, j;
char cmdbuf[MAX_INPUT_LENGTH]; // current line input buffer?
char F[2];   // temp search string
pchar lines[1+MAX_LINE_COUNT];
pchar progptr; // current character being interpreted
pchar q, x, y, s, d;

int evaluate(); // forward declaration

int parseterm(void) 
{
  int o ;
  switch(*progptr)
  {
  case '-':
  // negative term, skip sign and call negative
    progptr++;
    return -parseterm();
    break;
  // if it is a number :P
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':  
    // return longint
    return strtol( progptr, &progptr, 0);
  case '(':
    // evaluate expression between parentheses, skip ( and )
    progptr++;
    o = evaluate();
    progptr++;
    return o;
  case 'P':
    // this is the place for peeks, pdls, pins, I think!
    switch(*++progptr)
    {
    case 'D': // PDL(expr)
      progptr+=3;
      o = evaluate();
      progptr++;
      return analogRead(A0+o);
    case 'E': // PEEK(expr)
      progptr+=4;
      o = evaluate();
      progptr++;
      return *((int*)o);
    case 'I': // PIN(expr)
      progptr+=3;
      o = evaluate();
      progptr++;
      return digitalReadDirect(o);
    }
    // if we get here, it was none of the above, so it can be a variable
    progptr--;
    break;
  case 'R':
    switch(*++progptr)
    {
    case 'N': //  RND(expr)
      progptr+=3;
      o=evaluate();
      progptr++;
      return rand()%o;
    case 'E': // READKEY
      progptr+=7;
      byte oem;
      char key;
      //popKey(char * key, byte * oem)
      popKey(&key,&oem);
      return (int)key;
    }
    progptr--;
    break;
  case 'S': 
    switch(*++progptr)
    {
    case 'Q': //  SQRT(expr)
      progptr+=4;
      o=evaluate();
      progptr++;
      return sqrt(o);
    }
    progptr--;
    break;
  }
  // if not all that, it must be a variable!
  return var[*progptr++];
}

// add a new line
void addbasicline ()
{
  lineNum = atoi(cmdbuf); // string to int
  if(lines[lineNum]) // free line
  {
    bytes-=strlen(lines[lineNum]);
    free(lines[lineNum]);
  }

  // if there is more, 
  // allocate memory for a new basic line and copy it from B, 
  // else clear it and return zero
  if ((progptr = strstr ( cmdbuf , " " )))
  {
    bytes+=strlen(progptr);
    lines [ lineNum ] = (pchar)(malloc ( strlen ( progptr )));
    strcpy ((pchar)lines[lineNum], progptr + 1 );
  }
  else 
    lines[lineNum] = 0 ;
}


int muldiv () // W
{
  int o = parseterm();
  switch(*progptr++)
  {
  case '*': 
    return o * muldiv();
  case '/': 
    return o / muldiv();
  default:
    progptr--;
    return o;
  }
}
int addsub () // V
{
  int o = muldiv();
  switch(*progptr++)
  {
  case '+': 
    return o + addsub();
  case '-': 
    return o - addsub();
  default:
    progptr--;
    return o;
  }
} 
int greaterlessequal () // K
{
  int o = addsub();
  switch(*progptr++)
  {
  case '$':
    return o <= greaterlessequal();
  case '!':
    return o >= greaterlessequal();
  default:
    progptr--;
    return o;
  }

}

int lessgreater () // J
{
  int o = greaterlessequal();
  switch(*progptr++)
  {
  case '<':
    return o < lessgreater();
  case '>':
    return o > lessgreater();
  default:
    progptr--;
    return o;
  }
}

int evaluate () // S
{
  int o = lessgreater();
  switch(*progptr++)
  {
  case '=':
    return o == evaluate();
  case '#':
    return o != evaluate();
  default:
    progptr--;
    return o;
  }
}

int stepskips=0;
int usbskipper=64;
void usbTask(){
    if (stepskips++ > usbskipper ) {
        stepskips=0;
        usb.Task();
    }
}



void run()
{
  int tmp;
  gosub_stack_ptr = E; 
  lineNum = 1; // starting line
  // set all variables to zero
  for(i = 0; i < MAX_VARIABLES; var[i++] = 0);
  while(lineNum) // set to -1 to break loop
  {
    while(!(s = lines[lineNum])) {
      lineNum++;
    }
    if(!strstr(s, "\"")) // why the backslash
    {
      // replace double character operands with single char token, and make it a space 
      while(progptr = strstr(s, "<>")) *progptr++ = '#', *progptr = ' ';
      while(progptr = strstr(s, "!=")) *progptr++ = '#', *progptr = ' ';
      while(progptr = strstr(s, "<=")) *progptr++ = '$', *progptr = ' ';
      while(progptr = strstr(s, ">=")) *progptr++ = '!', *progptr = ' ';
    }
    d = cmdbuf;
    while(*F = *s)
    {
      if (*s == '"') 
        j++;
      if(j & 1 || !strstr(" \t", F)) 
        *d++ = *s;
      s++;
    }
    *d-- = j = 0;
    if(cmdbuf[1] != '=')
    {
      switch(*cmdbuf)
      {
      case 'E': // END
        lineNum = -1; // set line to -1 end while(l)
        break;

      case 'R': // REM and RETURN
        if ( cmdbuf[2] != 'M' ) // if it does not spell REM, it is RETURN
          lineNum = * --gosub_stack_ptr ;  // get return line from stack, decrease stack pointer
        break;

      case 'L': // LINE
        if (cmdbuf[1]=='I')
        {
          progptr = cmdbuf + 4;
          int sx=evaluate(); progptr++;
          int sy=evaluate(); progptr++;
          int ex=evaluate(); progptr++;
          int ey=evaluate(); progptr++;
          drawline(sx,sy,ex,ey,fgcolor);
        }
        break;
        
      case 'K':  // KEY
        if (cmdbuf[1]=='E') //  KEY -- wait for keypress
        {
          waitForKeyPress();
          progptr = cmdbuf + 3;
        }
        break;
      
      case 'C': // C
        if (cmdbuf[1]=='O') // COLOR expr expr
        {
          progptr = cmdbuf + 5;
          int cf=evaluate();
          progptr++;
          int cb=evaluate();
          fgcolor=cf;
          bgcolor=cb;
        } else if (cmdbuf[1]=='L') // CLS
        {
          progptr = cmdbuf + 3;
          cls();
        } else if (cmdbuf[1]=='U') // CURSOR
        {
          progptr = cmdbuf + 6;
          int x=evaluate();
          progptr++;
          int y=evaluate();
          gotoXY(x,y);
        }
        break;
      case 'I': // INPUT/IF/THEN
        //B[1] == 'N' ?
        //gets(p = B),
        //var[*d] =
        //evaluate()
        //: (*(q = strstr(B, "TH")) = 0, p = B + 2, evaluate() && (p = q + 4, l = evaluate() - 1));
        if (cmdbuf[1]=='N') // INPUT
        { 
          tmp = *d ; /* save for bug fix next line 07Sep2005 Somos */
          readLn ( progptr = cmdbuf ) ; 
          var[ tmp ] = evaluate ( ) ;
        } 
        else 
        {       //   IF
          // THEN
          *(q = strstr ( cmdbuf, "TH" )) = 0 ; 
          progptr = cmdbuf + 2 ;
          if ( evaluate ( ) ) 
          { 
            progptr = q + 4 ; 
            lineNum = evaluate ( ) - 1 ; 
          }
        }
        break;
      case 'S': // S
        if (cmdbuf[1] == 'E') // SETPIN
        {
          progptr=cmdbuf + 6;
          int pin = evaluate();
          progptr++;
          int value = evaluate();
          progptr++;
          digitalWriteDirect(pin,value%2);
        }
        break;
      case 'P': // P
        if ( cmdbuf[1] == 'L' ) // PLOT
        {
          progptr = cmdbuf + 4;
          int px=evaluate();
          progptr++;
          int py=evaluate();
          fb[py][px]=fgcolor;
        }
        else if ( cmdbuf[5] == '"' ) // PRINT STRING
        {
          *d=0 ; 
          
          int l=strlen(cmdbuf+6);
          if (*(cmdbuf+6+l)==';')
            write(cmdbuf + 6);
            
          else  
            writeLn ( cmdbuf + 6 ) ;
        } 
        else { // PRINT expr
          progptr = cmdbuf + 5 ;
          write(evaluate()); 
          //printf( "%d\n" , evaluate() ) ;
          if ((*progptr)!=';') // attempt to make print on a single line possible with ";" PRINT A;
            CRLF();
          else
            write(' ');
        }
        break;

      case 'G': // GOTO / GOSUB
        progptr = cmdbuf + 4 ;
        if (cmdbuf[2] == 'S') // GOSUB
        { 
          *gosub_stack_ptr++ = lineNum; // add current line number to stack
          progptr++ ; // skip B
        }
        lineNum = evaluate() - 1 ; // line number is evaluated
        break;

      case 'F': 
        if (cmdbuf[1]=='I') // FILL expr,expr,expr,expr
        {
          progptr = cmdbuf + 4;
          int sx=evaluate(); progptr++;
          int sy=evaluate(); progptr++;
          int ex=evaluate(); progptr++;
          int ey=evaluate(); progptr++;
          VGAfill(sx,sy,ex,ey,fgcolor);
          
        } else if (cmdbuf[1]=='O')
        {
          // FOR var = exp1 TO exp2
          // set q to >"TO.., mark end of "var = exp1"
          *(q = strstr(cmdbuf, "TO")) = 0;
          // set p to >"var..
          progptr = cmdbuf + 5;
          // evaluate the expression
          var[i = cmdbuf[3]] = evaluate();
          progptr = q + 2;
          M[i] = evaluate();
          L[i] = lineNum;
        }
        break;

      case 'N': // NEXT
        if (++var[*d] <= M[*d]) 
          lineNum = L[*d];

      }
    }
    else // direct VARIABLE assignment
    {
      progptr = cmdbuf + 2;
      var[ *cmdbuf ] = evaluate();
    }
    lineNum++; // next line!
    usbTask();

  };
}


void basic_main ()
{
  lines[MAX_LINE_COUNT] = "E"; // put and end to it
  while(write(">"), readLn(cmdbuf))
  {
    switch(*cmdbuf)
    {
    case 'R':
      run();
      break;

    case 'L': // LIST
      for(i = 0; i < MAX_LINE_COUNT; i++) 
        if (lines[i])
        {
          //printf("%d %s\n", i, lines[i]);
          write(i); write(' '); writeLn(lines[i]);
        }
      break;

    case 'N': // NEW
      for(i = 0; i < MAX_LINE_COUNT; i++)
      {
        if (lines[i]) 
        {
          bytes -= strlen(lines[i]);
          free(lines[i]);
          lines[i] = 0;
        }
      }
      break;
    case 'E': // set usb skip cycles
     if (usbskipper>2) usbskipper/=2;
      write("usb.Task() skip cycles: "); write(usbskipper); writeLn(" cycles");
      break;
    case 'e': // set usb skip cycles
     if (usbskipper<=65536) usbskipper*=2;
     write("usb.Task() skip cycles: "); write(usbskipper); writeLn(" cycles");
      break;
    case 'B': // BYE
      return;
      break;

    case 'U': // USED
      write("used "); write(bytes); writeLn(" bytes");
      break;
    case 'C': // CLS
      cls();
      break;
    case 'D': // DIR
      dir(cmdbuf+4);
      break;
    case 'S': // SAVE
      if (initCard())
      {
        if (file.open(cmdbuf+5,O_WRITE|O_CREAT))
        {
          for(i = 0; i < MAX_LINE_COUNT; i++) 
            if(lines[i])
            {
              itoa_(i, itoabuf);
              write(itoabuf); write(" "); writeLn(lines[i]);
              
              file.write(itoabuf); file.write(" "); file.println(lines[i]); 
            }
          
        }  else fsError(FS_WRITE_ERROR);
        doneCard();
      }
      break;
    case 'M': // MOUNT
      mount();      
      break;
    case 'O': // OLD (OLD)
    // should we not 'NEW' before all this?
      if (initCard())
      {
        if (file.open(cmdbuf+4,O_READ))
        {
          while (file.fgets(cmdbuf,MAX_INPUT_LENGTH))
          {
            *strstr(cmdbuf,"\n") = 0;
            addbasicline();
          }
        } else fsError(FS_OPEN_ERROR);
        doneCard();
      }
      break;

    case 0:
    default:
      addbasicline();
    }
    usbTask();
  }
  return;
}



