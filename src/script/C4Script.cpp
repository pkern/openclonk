/*
 * OpenClonk, http://www.openclonk.org
 *
 * Copyright (c) 1998-2000, Matthes Bender
 * Copyright (c) 2001-2009, RedWolf Design GmbH, http://www.clonk.de/
 * Copyright (c) 2009-2013, The OpenClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

/* Functions mapped by C4Script */

#include <C4Include.h>
#include <C4AulDefFunc.h>

#include <C4AulExec.h>
#include <C4Random.h>
#include <C4Version.h>

//========================== Some Support Functions =======================================

StdStrBuf FnStringFormat(C4PropList * _this, C4String *szFormatPar, C4Value * Pars, int ParCount)
{
	int cPar=0;

	StdStrBuf StringBuf("", false);
	const char * cpFormat = FnStringPar(szFormatPar);
	const char * cpType;
	char szField[20];
	while (*cpFormat)
	{
		// Copy normal stuff
		while (*cpFormat && (*cpFormat!='%'))
			StringBuf.AppendChar(*cpFormat++);
		// Field
		if (*cpFormat=='%')
		{
			// Scan field type
			for (cpType=cpFormat+1; *cpType && (*cpType == '+' || *cpType == '-' || *cpType == '.' || *cpType == '#' || *cpType == ' ' || Inside(*cpType,'0','9')); cpType++) {}
			// Copy field
			SCopy(cpFormat,szField,std::min<unsigned int>(sizeof szField - 1, cpType - cpFormat + 1));
			// Insert field by type
			switch (*cpType)
			{
				// number
			case 'd': case 'x': case 'X':
			{
				if (cPar >= ParCount) throw C4AulExecError("format placeholder without parameter");
				StringBuf.AppendFormat(szField, Pars[cPar++].getInt());
				cpFormat+=SLen(szField);
				break;
			}
			// character
			case 'c':
			{
				if (cPar >= ParCount) throw C4AulExecError("format placeholder without parameter");
				StringBuf.AppendCharacter(Pars[cPar++].getInt());
				cpFormat+=SLen(szField);
				break;
			}
			// C4ID
			case 'i':
			// C4Value
			case 'v':
			{
				if (cPar >= ParCount) throw C4AulExecError("format placeholder without parameter");
				StringBuf.Append(static_cast<const StdStrBuf&>(Pars[cPar++].GetDataString(10)));
				cpFormat+=SLen(szField);
				break;
			}
			// String
			case 's':
			{
				// get string
				if (cPar >= ParCount) throw C4AulExecError("format placeholder without parameter");
				const char *szStr = "(null)";
				if (Pars[cPar].GetData())
				{
					C4String * pStr = Pars[cPar].getStr();
					if (!pStr) throw C4AulExecError("string format placeholder without string");
					szStr = pStr->GetCStr();
				}
				++cPar;
				StringBuf.AppendFormat(szField, szStr);
				cpFormat+=SLen(szField);
				break;
			}
			case '%':
				StringBuf.AppendChar('%');
				cpFormat+=SLen(szField);
				break;
				// Undefined / Empty
			default:
				StringBuf.AppendChar('%');
				cpFormat++;
				break;
			}
		}
	}
	return StringBuf;
}

C4AulDefFunc::C4AulDefFunc(C4PropListStatic * Parent, C4ScriptFnDef* pDef):
		C4AulFunc(Parent, pDef->Identifier), Def(pDef)
{
	Parent->SetPropertyByS(Name, C4VFunction(this));
}

C4AulDefFunc::~C4AulDefFunc()
{
}

C4Value C4AulDefFunc::Exec(C4PropList * p, C4Value pPars[], bool fPassErrors)
{
	assert(Def->FunctionC4V);
	return Def->FunctionC4V(p, pPars);
}

//=============================== C4Script Functions ====================================

#define MAKE_AND_RETURN_ARRAY(values) do { \
	C4ValueArray *matrix = new C4ValueArray(sizeof(values) / sizeof(*values)); \
	for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) \
		(*matrix)[i] = C4VInt(values[i]); \
	return matrix; \
} while (0)

static C4ValueArray *FnTrans_Identity(C4PropList * _this)
{
	long values[] = 
	{
		1000, 0, 0, 0,
		0, 1000, 0, 0,
		0, 0, 1000, 0
	};
	MAKE_AND_RETURN_ARRAY(values);
}

static C4ValueArray *FnTrans_Translate(C4PropList * _this, long dx, long dy, long dz)
{
	long values[] = 
	{
		1000, 0, 0, dx,
		0, 1000, 0, dy,
		0, 0, 1000, dz
	};
	MAKE_AND_RETURN_ARRAY(values);
}

static C4ValueArray *FnTrans_Scale(C4PropList * _this, long sx, long sy, long sz)
{
	if (sy == 0 && sz == 0)
		sy = sz = sx;
	long values[] = 
	{
		sx, 0, 0, 0,
		0, sy, 0, 0,
		0, 0, sz, 0
	};
	MAKE_AND_RETURN_ARRAY(values);
}

static C4ValueArray *FnTrans_Rotate(C4PropList * _this, long angle, long rx, long ry, long rz)
{
	long c = fixtoi(Cos(itofix(angle, 1)), 1000);
	long s = fixtoi(Sin(itofix(angle, 1)), 1000);

	long sqrt_val = rx * rx + ry * ry + rz * rz;
	long n = long(sqrt(double(sqrt_val)));
	if (n * n < sqrt_val) n++;
	else if (n * n > sqrt_val) n--;

	rx = (1000 * rx) / n;
	ry = (1000 * ry) / n;
	rz = (1000 * rz) / n;

	long values[] = 
	{
		rx*rx*(1000-c)/1000000+c, rx*ry*(1000-c)/1000000-rz*s/1000, rx*rz*(1000-c)/1000000+ry*s/1000, 0,
		ry*rx*(1000-c)/1000000+rz*s/1000, ry*ry*(1000-c)/1000000+c, ry*rz*(1000-c)/1000000-rx*s/1000, 0,
		rz*rx*(1000-c)/1000000-ry*s/1000, ry*rz*(1000-c)/1000000+rx*s/1000, rz*rz*(1000-c)/1000000+c, 0
	};
	MAKE_AND_RETURN_ARRAY(values);
}

static C4Value FnTrans_Mul(C4PropList * _this, C4Value *pars)
{
	const int32_t matrixSize = 12;
	long values[] = 
	{
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0
	};

	// Read all parameters
	bool first = true;
	for (int32_t i = 0; i < C4AUL_MAX_Par; i++)
	{
		C4Value Data = *(pars++);
		// No data given?
		if (!Data) break;
		C4ValueArray *factorArray = Data.getArray();
		if (!factorArray || factorArray->GetSize() != matrixSize) continue;

		if (first)
		{
			first = false;
			
			for (int32_t c = 0; c < matrixSize; ++c)
				values[c] = (*factorArray)[c].getInt();
			continue;
		}

		// multiply current matrix with new one
		long values_rhs[matrixSize], values_result[matrixSize];
		for (int32_t c = 0; c < matrixSize; ++c)
			values_rhs[c] = (*factorArray)[c].getInt();

		// matrix multiplication
		values_result[ 0] = values[0]*values_rhs[0]/1000 + values[1]*values_rhs[4]/1000 + values[ 2]*values_rhs[ 8]/1000;
		values_result[ 1] = values[0]*values_rhs[1]/1000 + values[1]*values_rhs[5]/1000 + values[ 2]*values_rhs[ 9]/1000;
		values_result[ 2] = values[0]*values_rhs[2]/1000 + values[1]*values_rhs[6]/1000 + values[ 2]*values_rhs[10]/1000;
		values_result[ 3] = values[0]*values_rhs[3]/1000 + values[1]*values_rhs[7]/1000 + values[ 2]*values_rhs[11]/1000 + values[3];
		values_result[ 4] = values[4]*values_rhs[0]/1000 + values[5]*values_rhs[4]/1000 + values[ 6]*values_rhs[ 8]/1000;
		values_result[ 5] = values[4]*values_rhs[1]/1000 + values[5]*values_rhs[5]/1000 + values[ 6]*values_rhs[ 9]/1000;
		values_result[ 6] = values[4]*values_rhs[2]/1000 + values[5]*values_rhs[6]/1000 + values[ 6]*values_rhs[10]/1000;
		values_result[ 7] = values[4]*values_rhs[3]/1000 + values[5]*values_rhs[7]/1000 + values[ 6]*values_rhs[11]/1000 + values[7];
		values_result[ 8] = values[8]*values_rhs[0]/1000 + values[9]*values_rhs[4]/1000 + values[10]*values_rhs[ 8]/1000;
		values_result[ 9] = values[8]*values_rhs[1]/1000 + values[9]*values_rhs[5]/1000 + values[10]*values_rhs[ 9]/1000;
		values_result[10] = values[8]*values_rhs[2]/1000 + values[9]*values_rhs[6]/1000 + values[10]*values_rhs[10]/1000;
		values_result[11] = values[8]*values_rhs[3]/1000 + values[9]*values_rhs[7]/1000 + values[10]*values_rhs[11]/1000 + values[11];

		for (int32_t c = 0; c < matrixSize; ++c)
			values[c] = values_result[c];
	}

	// unlike in the other Trans_*-functions, we have to put the array into a C4Value manually here
	C4ValueArray *matrix = new C4ValueArray(sizeof(values) / sizeof(*values));
	for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i)
		(*matrix)[i] = C4VInt(values[i]);
	return C4VArray(matrix);
}

#undef MAKE_AND_RETURN_ARRAY

static C4PropList * FnCreatePropList(C4PropList * _this, C4PropList * prototype)
{
	return C4PropList::New(prototype);
}

static C4Value FnGetProperty(C4PropList * _this, C4String * key, C4PropList * pObj)
{
	if (!pObj) pObj = _this;
	if (!pObj) return C4VNull;
	if (!key) return C4VNull;
	C4Value r;
	pObj->GetPropertyByS(key, &r);
	return r;
}

static bool FnSetProperty(C4PropList * _this, C4String * key, const C4Value & to, C4PropList * pObj)
{
	if (!pObj) pObj = _this;
	if (!pObj) return false;
	if (!key) return false;
	if (pObj->IsFrozen())
		throw C4AulExecError("proplist write: proplist is readonly");
	pObj->SetPropertyByS(key, to);
	return true;
}

static bool FnResetProperty(C4PropList * _this, C4String * key, C4PropList * pObj)
{
	if (!pObj) pObj = _this;
	if (!pObj) return false;
	if (!key) return false;
	if (!pObj->HasProperty(key)) return false;
	if (pObj->IsFrozen())
		throw C4AulExecError("proplist write: proplist is readonly");
	pObj->ResetProperty(key);
	return true;
}

static C4ValueArray * FnGetProperties(C4PropList * _this, C4PropList * p)
{
	if (!p) p = _this;
	if (!p) throw NeedNonGlobalContext("GetProperties");
	C4ValueArray * r = p->GetProperties();
	r->SortStrings();
	return r;
}

static C4Value FnCall(C4PropList * _this, C4Value * Pars)
{
	if (!_this) _this = ::ScriptEngine.GetPropList();
	C4AulParSet ParSet;
	ParSet.Copy(&Pars[1], C4AUL_MAX_Par - 1);
	C4AulFunc * fn = Pars[0].getFunction();
	C4String * name;
	if (!fn)
	{
		name = Pars[0].getStr();
		if (name) fn = _this->GetFunc(name);
	}
	if (!fn)
	{
		const char * s = FnStringPar(name);
		if (s[0] == '~')
		{
			fn = _this->GetFunc(&s[1]);
			if (!fn)
				return C4Value();
		}
	}
	if (!fn)
		throw C4AulExecError(FormatString("Call: no function %s", Pars[0].GetDataString().getData()).getData());
	return fn->Exec(_this, &ParSet, true);
}

static C4Value FnLog(C4PropList * _this, C4Value * Pars)
{
	Log(FnStringFormat(_this, Pars[0].getStr(), &Pars[1], 9).getData());
	return C4VBool(true);
}

static C4Value FnDebugLog(C4PropList * _this, C4Value * Pars)
{
	DebugLog(FnStringFormat(_this, Pars[0].getStr(), &Pars[1], 9).getData());
	return C4VBool(true);
}

static C4Value FnFormat(C4PropList * _this, C4Value * Pars)
{
	return C4VString(FnStringFormat(_this, Pars[0].getStr(), &Pars[1], 9));
}

static long FnAbs(C4PropList * _this, long iVal)
{
	return Abs(iVal);
}

static long FnSin(C4PropList * _this, long iAngle, long iRadius, long iPrec)
{
	if (!iPrec) iPrec = 1;
	// Precalculate the modulo operation so the C4Fixed argument to Sin does not overflow
	iAngle %= 360 * iPrec;
	// Let itofix and fixtoi handle the division and multiplication because that can handle higher ranges
	return fixtoi(Sin(itofix(iAngle, iPrec)), iRadius);
}

static long FnCos(C4PropList * _this, long iAngle, long iRadius, long iPrec)
{
	if (!iPrec) iPrec = 1;
	iAngle %= 360 * iPrec;
	return fixtoi(Cos(itofix(iAngle, iPrec)), iRadius);
}

static long FnSqrt(C4PropList * _this, long iValue)
{
	if (iValue<0) return 0;
	long iSqrt = long(sqrt(double(iValue)));
	if (iSqrt * iSqrt < iValue) iSqrt++;
	if (iSqrt * iSqrt > iValue) iSqrt--;
	return iSqrt;
}

static long FnAngle(C4PropList * _this, long iX1, long iY1, long iX2, long iY2, long iPrec)
{
	long iAngle;

	// Standard prec
	if (!iPrec) iPrec = 1;

	long dx=iX2-iX1,dy=iY2-iY1;
	if (!dx)
	{
		if (dy>0) return 180 * iPrec;
		else return 0;
	}
	if (!dy)
	{
		if (dx>0) return 90 * iPrec;
		else return 270 * iPrec;
	}

	iAngle = static_cast<long>(180.0 * iPrec * atan2(static_cast<double>(Abs(dy)), static_cast<double>(Abs(dx))) / M_PI);

	if (iX2>iX1 )
	{
		if (iY2<iY1) iAngle = (90 * iPrec) - iAngle;
		else iAngle = (90 * iPrec) + iAngle;
	}
	else
	{
		if (iY2<iY1) iAngle = (270 * iPrec) + iAngle;
		else iAngle = (270 * iPrec) - iAngle;
	}

	return iAngle;
}

static long FnArcSin(C4PropList * _this, long iVal, long iRadius)
{
	// safety
	if (!iRadius) return 0;
	if (iVal > iRadius) return 0;
	// calc arcsin
	double f1 = iVal;
	f1 = asin(f1/iRadius)*180.0/M_PI;
	// return rounded angle
	return (long) floor(f1+0.5);
}

static long FnArcCos(C4PropList * _this, long iVal, long iRadius)
{
	// safety
	if (!iRadius) return 0;
	if (iVal > iRadius) return 0;
	// calc arccos
	double f1 = iVal;
	f1 = acos(f1/iRadius)*180.0/M_PI;
	// return rounded angle
	return (long) floor(f1+0.5);
}

static long FnMin(C4PropList * _this, long iVal1, long iVal2)
{
	return std::min(iVal1,iVal2);
}

static long FnMax(C4PropList * _this, long iVal1, long iVal2)
{
	return std::max(iVal1,iVal2);
}

static long FnDistance(C4PropList * _this, long iX1, long iY1, long iX2, long iY2)
{
	return Distance(iX1,iY1,iX2,iY2);
}

static long FnBoundBy(C4PropList * _this, long iVal, long iRange1, long iRange2)
{
	return Clamp(iVal,iRange1,iRange2);
}

static bool FnInside(C4PropList * _this, long iVal, long iRange1, long iRange2)
{
	return Inside(iVal,iRange1,iRange2);
}

static long FnRandom(C4PropList * _this, long iRange)
{
	return Random(iRange);
}

static long FnAsyncRandom(C4PropList * _this, long iRange)
{
	return SafeRandom(iRange);
}

static int FnGetType(C4PropList * _this, const C4Value & Value)
{
	// dynamic types
	if (Value.CheckConversion(C4V_Object)) return C4V_Object;
	if (Value.CheckConversion(C4V_Def)) return C4V_Def;
	if (Value.CheckConversion(C4V_Effect)) return C4V_Effect;
	// static types
	return Value.GetType();
}

static C4ValueArray * FnCreateArray(C4PropList * _this, int iSize)
{
	return new C4ValueArray(iSize);
}

static int FnGetLength(C4PropList * _this, const C4Value & Par)
{
	// support GetLength() etc.
	C4ValueArray * pArray = Par.getArray();
	if (pArray)
		return pArray->GetSize();
	C4String * pStr = Par.getStr();
	if (pStr)
		return GetCharacterCount(pStr->GetData().getData());
	throw C4AulExecError("GetLength: parameter 0 cannot be converted to string or array");
}

static int FnGetIndexOf(C4PropList * _this, C4ValueArray * pArray, const C4Value & Needle)
{
	// find first occurance of first parameter in array
	// support GetIndexOf(0, x)
	if (!pArray) return -1;
	int32_t iSize = pArray->GetSize();
	for (int32_t i = 0; i < iSize; ++i)
		if (Needle.IsIdenticalTo(pArray->GetItem(i)))
			// element found
			return i;
	// element not found
	return -1;
}

static bool FnDeepEqual(C4PropList * _this, const C4Value & v1, const C4Value & v2)
{
	// return if v1==v2 with deep comparison on arrays and proplists
	return v1 == v2;
}

static C4Void FnSetLength(C4PropList * _this, C4ValueArray *pArray, int iNewSize)
{
	// safety
	if (iNewSize<0 || iNewSize > C4ValueArray::MaxSize)
		throw C4AulExecError(FormatString("SetLength: invalid array size (%d)", iNewSize).getData());

	// set new size
	pArray->SetSize(iNewSize);
	return C4Void();
}

static Nillable<long> FnGetChar(C4PropList * _this, C4String *pString, long iIndex)
{
	const char *szText = FnStringPar(pString);
	if (!szText) return C4Void();
	// C4Strings are UTF-8 encoded, so decode to get the indicated character
	uint32_t c = GetNextCharacter(&szText);
	for (int i = 0; i < iIndex; ++i)
	{
		c = GetNextCharacter(&szText);
		if (!c) return C4Void();
	}
	return c;
}

static C4Value Fneval(C4PropList * _this, C4String *strScript)
{
	// execute script in the same object
	if (Object(_this))
		return Object(_this)->Def->Script.DirectExec(Object(_this), FnStringPar(strScript), "eval", true);
	else if (_this && _this->GetDef())
		return _this->GetDef()->Script.DirectExec(0, FnStringPar(strScript), "eval", true);
	else
		return ::GameScript.DirectExec(0, FnStringPar(strScript), "eval", true);
}

static bool FnLocateFunc(C4PropList * _this, C4String *funcname, C4PropList * p)
{
	// safety
	if (!funcname || !funcname->GetCStr())
	{
		Log("No func name");
		return false;
	}
	if (!p) p = _this;
	// get function by name
	C4AulFunc *pFunc = p->GetFunc(funcname);
	if (!pFunc)
	{
		LogF("Func %s not found", funcname->GetCStr());
	}
	else
	{
		const char *szPrefix = "";
		while (pFunc)
		{
			C4AulScriptFunc *pSFunc = pFunc->SFunc();
			if (!pSFunc)
			{
				LogF("%s%s (engine)", szPrefix, pFunc->GetName());
			}
			else if (!pSFunc->pOrgScript)
			{
				LogF("%s%s (no owner)", szPrefix, pSFunc->GetName());
			}
			else
			{
				int32_t iLine = SGetLine(pSFunc->pOrgScript->GetScript(), pSFunc->Script);
				LogF("%s%s (%s:%d)", szPrefix, pFunc->GetName(), pSFunc->pOrgScript->ScriptName.getData(), (int)iLine);
			}
			// next func in overload chain
			pFunc = pSFunc ? pSFunc->OwnerOverloaded : NULL;
			szPrefix = "overloads ";
		}
	}
	return true;
}

static long FnModulateColor(C4PropList * _this, long iClr1, long iClr2)
{
	DWORD dwClr1 = iClr1;
	DWORD dwClr2 = iClr2;
	// default color
	if (!dwClr1) dwClr1 = 0xffffff;
	// get alpha
	long iA1=dwClr1>>24, iA2=dwClr2>>24;
	// modulate color values; mod alpha upwards
	DWORD r = (((dwClr1     & 0xff) * (dwClr2    &   0xff))    >>  8)   | // blue
	          (((dwClr1>> 8 & 0xff) * (dwClr2>>8 &   0xff)) &   0xff00) | // green
	          (((dwClr1>>16 & 0xff) * (dwClr2>>8 & 0xff00)) & 0xff0000) | // red
	          (std::min<long>(iA1+iA2 - ((iA1*iA2)>>8), 255)           << 24); // alpha
	return r;
}

static long FnWildcardMatch(C4PropList * _this, C4String *psString, C4String *psWildcard)
{
	return SWildcardMatchEx(FnStringPar(psString), FnStringPar(psWildcard));
}

static bool FnFatalError(C4PropList * _this, C4String *pErrorMsg)
{
	throw C4AulExecError(FormatString("script: %s", pErrorMsg ? pErrorMsg->GetCStr() : "(no error)").getData());
}

static bool FnStartCallTrace(C4PropList * _this)
{
	extern void C4AulStartTrace();
	C4AulStartTrace();
	return true;
}

static bool FnStartScriptProfiler(C4PropList * _this, C4Def * pDef)
{
	// get script to profile
	C4AulScript *pScript;
	if (pDef)
		pScript = &pDef->Script;
	else
		pScript = &::ScriptEngine;
	// profile it
	C4AulProfiler::StartProfiling(pScript);
	return true;
}

static bool FnStopScriptProfiler(C4PropList * _this)
{
	C4AulProfiler::StopProfiling();
	return true;
}

static Nillable<C4String *> FnGetConstantNameByValue(C4PropList * _this, int value, Nillable<C4String *> name_prefix, int idx)
{
	C4String *name_prefix_s = name_prefix;
	// find a constant that has the specified value and prefix
	for (int32_t i = 0; i < ::ScriptEngine.GlobalConsts.GetAnzItems(); ++i)
	{
		if (::ScriptEngine.GlobalConsts[i].getInt() == value)
		{
			const char *const_name = ::ScriptEngine.GlobalConstNames.GetItemUnsafe(i);
			if (!name_prefix_s || SEqual2(const_name, name_prefix_s->GetCStr()))
				if (!idx--)
					// indexed constant found. return name minus prefix
					return String(const_name + (name_prefix_s ? name_prefix_s->GetData().getLength() : 0));
		}
	}
	// nothing found (at index)
	return C4Void();
}

static bool FnSortArray(C4PropList * _this, C4ValueArray *pArray, bool descending)
{
	if (!pArray) throw C4AulExecError("SortArray: no array given");
	// sort array by its members
	pArray->Sort(descending);
	return true;
}

static bool FnSortArrayByProperty(C4PropList * _this, C4ValueArray *pArray, C4String *prop_name, bool descending)
{
	if (!pArray) throw C4AulExecError("SortArrayByProperty: no array given");
	if (!prop_name) throw C4AulExecError("SortArrayByProperty: no property name given");
	// sort array by property
	if (!pArray->SortByProperty(prop_name, descending)) throw C4AulExecError("SortArrayByProperty: not all array elements are proplists");
	return true;
}

static bool FnSortArrayByArrayElement(C4PropList * _this, C4ValueArray *pArray, int32_t element_index, bool descending)
{
	if (!pArray) throw C4AulExecError("SortArrayByArrayElement: no array given");
	if (element_index<0) throw C4AulExecError("SortArrayByArrayElement: element index must be >=0");
	// sort array by array element
	if (!pArray->SortByArrayElement(element_index, descending)) throw C4AulExecError("SortArrayByArrayElement: not all array elements are arrays of sufficient length");
	return true;
}

static bool FnFileWrite(C4PropList * _this, int32_t file_handle, C4String *data)
{
	// resolve file handle to user file
	C4AulUserFile *file = ::ScriptEngine.GetUserFile(file_handle);
	if (!file) throw C4AulExecError("FileWrite: invalid file handle");
	// prepare string to write
	if (!data) return false; // write NULL? No.
	// write it
	file->Write(data->GetCStr(), data->GetData().getLength());
	return true;
}

//=========================== C4Script Function Map ===================================

C4ScriptConstDef C4ScriptConstMap[]=
{
	{ "C4V_Nil",         C4V_Int, C4V_Nil},
	{ "C4V_Int",         C4V_Int, C4V_Int},
	{ "C4V_Bool",        C4V_Int, C4V_Bool},
	{ "C4V_C4Object",    C4V_Int, C4V_Object},
	{ "C4V_Effect",      C4V_Int, C4V_Effect},
	{ "C4V_Def",         C4V_Int, C4V_Def},
	{ "C4V_String",      C4V_Int, C4V_String},
	{ "C4V_Array",       C4V_Int, C4V_Array},
	{ "C4V_Function",    C4V_Int, C4V_Function},
	{ "C4V_PropList",    C4V_Int, C4V_PropList},

	{ "C4X_Ver1",        C4V_Int, C4XVER1},
	{ "C4X_Ver2",        C4V_Int, C4XVER2},

	{ NULL, C4V_Nil, 0}
};

C4ScriptFnDef C4ScriptFnMap[]=
{
	{ "Call",          1, C4V_Any,    { C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any    ,C4V_Any    ,C4V_Any    ,C4V_Any}, FnCall     },
	{ "Log",           1, C4V_Bool,   { C4V_String  ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any    ,C4V_Any    ,C4V_Any    ,C4V_Any}, FnLog      },
	{ "DebugLog",      1, C4V_Bool,   { C4V_String  ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any    ,C4V_Any    ,C4V_Any    ,C4V_Any}, FnDebugLog },
	{ "Format",        1, C4V_String, { C4V_String  ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any    ,C4V_Any    ,C4V_Any    ,C4V_Any}, FnFormat   },
	{ "Trans_Mul",     1, C4V_Array,  { C4V_Array   ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any     ,C4V_Any    ,C4V_Any    ,C4V_Any    ,C4V_Any}, FnTrans_Mul},

	{ NULL,            0, C4V_Nil,    { C4V_Nil     ,C4V_Nil     ,C4V_Nil     ,C4V_Nil     ,C4V_Nil     ,C4V_Nil     ,C4V_Nil    ,C4V_Nil    ,C4V_Nil    ,C4V_Nil}, 0          }
};

void InitCoreFunctionMap(C4AulScriptEngine *pEngine)
{
	// add all def constants (all Int)
	for (C4ScriptConstDef *pCDef = &C4ScriptConstMap[0]; pCDef->Identifier; pCDef++)
	{
		assert(pCDef->ValType == C4V_Int); // only int supported currently
		pEngine->RegisterGlobalConstant(pCDef->Identifier, C4VInt(pCDef->Data));
	}

	// add all def script funcs
	for (C4ScriptFnDef *pDef = &C4ScriptFnMap[0]; pDef->Identifier; pDef++)
		new C4AulDefFunc(pEngine->GetPropList(), pDef);
#define F(f) AddFunc(pEngine, #f, Fn##f)
	F(Abs);
	F(Min);
	F(Max);
	F(Sin);
	F(Cos);
	F(Sqrt);
	F(ArcSin);
	F(ArcCos);
	F(BoundBy);
	F(Inside);
	F(Random);
	F(AsyncRandom);

	F(CreateArray);
	F(CreatePropList);
	F(GetProperties);
	F(GetProperty);
	F(SetProperty);
	F(ResetProperty);
	F(Distance);
	F(Angle);
	F(GetChar);
	F(GetType);
	F(ModulateColor);
	F(WildcardMatch);
	F(GetLength);
	F(SetLength);
	F(GetIndexOf);
	F(DeepEqual);
	F(FatalError);
	F(StartCallTrace);
	F(StartScriptProfiler);
	F(StopScriptProfiler);
	F(SortArray);
	F(SortArrayByProperty);
	F(SortArrayByArrayElement);
	F(Trans_Identity);
	F(Trans_Translate);
	F(Trans_Scale);
	F(Trans_Rotate);
	F(LocateFunc);
	F(FileWrite);

	F(eval);
	F(GetConstantNameByValue);

	AddFunc(pEngine, "Translate", C4AulExec::FnTranslate);
	AddFunc(pEngine, "LogCallStack", C4AulExec::FnLogCallStack);
#undef F
}
