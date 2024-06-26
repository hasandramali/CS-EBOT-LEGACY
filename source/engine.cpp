//
// Copyright (c) 2003-2009, by Yet Another POD-Bot Development Team.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// $Id: engine.cpp 35 2009-06-24 16:43:26Z jeefo $
//

#include <core.h>

ConVar::ConVar(const char* name, const char* initval, VarType type)
{
    engine->RegisterVariable(name, initval, type, this);
}

void Engine::RegisterVariable(const char* variable, const char* value, VarType varType, ConVar* self)
{
    VarPair newVariable;

    newVariable.reg.name = const_cast <char*> (variable);
    newVariable.reg.string = const_cast <char*> (value);

    int engineFlags = FCVAR_EXTDLL;

    if (varType == VARTYPE_NORMAL)
        engineFlags |= FCVAR_SERVER;
    else if (varType == VARTYPE_READONLY)
        engineFlags |= FCVAR_SERVER | FCVAR_SPONLY | FCVAR_PRINTABLEONLY;
    else if (varType == VARTYPE_PASSWORD)
        engineFlags |= FCVAR_PROTECTED;

    newVariable.reg.flags = engineFlags;
    newVariable.self = self;

    cmemcpy(&m_regVars[m_regCount], &newVariable, sizeof(VarPair));
    m_regCount++;
}

void Engine::PushRegisteredConVarsToEngine(void)
{
    for (int i = 0; i < m_regCount; i++)
    {
        VarPair* ptr = &m_regVars[i];

        if (ptr == nullptr)
            break;

        g_engfuncs.pfnCVarRegister(&ptr->reg);
        ptr->self->m_eptr = g_engfuncs.pfnCVarGetPointer(ptr->reg.name);
    }
}

void Engine::GetGameConVarsPointers(void)
{
    m_gameVars[GVAR_C4TIMER] = g_engfuncs.pfnCVarGetPointer("mp_c4timer");
    m_gameVars[GVAR_BUYTIME] = g_engfuncs.pfnCVarGetPointer("mp_buytime");
    m_gameVars[GVAR_FRIENDLYFIRE] = g_engfuncs.pfnCVarGetPointer("mp_friendlyfire");
    m_gameVars[GVAR_ROUNDTIME] = g_engfuncs.pfnCVarGetPointer("mp_roundtime");
    m_gameVars[GVAR_FREEZETIME] = g_engfuncs.pfnCVarGetPointer("mp_freezetime");
    m_gameVars[GVAR_FOOTSTEPS] = g_engfuncs.pfnCVarGetPointer("mp_footsteps");
    m_gameVars[GVAR_GRAVITY] = g_engfuncs.pfnCVarGetPointer("sv_gravity");
    m_gameVars[GVAR_DEVELOPER] = g_engfuncs.pfnCVarGetPointer("developer");

    // if buytime is null, just set it to round time
    if (m_gameVars[GVAR_BUYTIME] == nullptr)
        m_gameVars[GVAR_BUYTIME] = m_gameVars[3];
}

const Vector& Engine::GetGlobalVector(GlobalVector id)
{
    switch (id)
    {
    case GLOBALVECTOR_FORWARD:
        return g_pGlobals->v_forward;

    case GLOBALVECTOR_RIGHT:
        return g_pGlobals->v_right;

    case GLOBALVECTOR_UP:
        return g_pGlobals->v_up;
    }
    return nullvec;
}

void Engine::SetGlobalVector(GlobalVector id, const Vector& newVector)
{
    switch (id)
    {
    case GLOBALVECTOR_FORWARD:
        g_pGlobals->v_forward = newVector;
        break;

    case GLOBALVECTOR_RIGHT:
        g_pGlobals->v_right = newVector;
        break;

    case GLOBALVECTOR_UP:
        g_pGlobals->v_up = newVector;
        break;
    }
}

void Engine::BuildGlobalVectors(const Vector& on)
{
    on.BuildVectors(&g_pGlobals->v_forward, &g_pGlobals->v_right, &g_pGlobals->v_up);
}

bool Engine::IsFootstepsOn(void)
{
    return m_gameVars[GVAR_FOOTSTEPS]->value > 0;
}

float Engine::GetC4TimerTime(void)
{
    return m_gameVars[GVAR_C4TIMER]->value;
}

float Engine::GetBuyTime(void)
{
    return m_gameVars[GVAR_BUYTIME]->value;
}

float Engine::GetRoundTime(void)
{
    return m_gameVars[GVAR_ROUNDTIME]->value;
}

float Engine::GetFreezeTime(void)
{
    return m_gameVars[GVAR_FREEZETIME]->value;
}

int Engine::GetGravity(void)
{
    return static_cast <int> (m_gameVars[GVAR_GRAVITY]->value);
}

int Engine::GetDeveloperLevel(void)
{
    return static_cast <int> (m_gameVars[GVAR_DEVELOPER]->value);
}

bool Engine::IsFriendlyFireOn(void)
{
    return m_gameVars[GVAR_FRIENDLYFIRE]->value > 0;
}

void Engine::PrintServer(const char* format, ...)
{
    static char buffer[1024];
    va_list ap;

    va_start(ap, format);
    vsprintf(buffer, format, ap);
    va_end(ap);

    cstrcat(buffer, "\n");

    g_engfuncs.pfnServerPrint(buffer);
}

int Engine::GetMaxClients(void)
{
    return g_pGlobals->maxClients;
}

float Engine::GetTime(void)
{
    return g_pGlobals->time;
}

void Engine::PrintAllClients(PrintType printType, const char* format, ...)
{
    char buffer[1024];
    va_list ap;

    va_start(ap, format);
    vsprintf(buffer, format, ap);
    va_end(ap);

    if (printType == PRINT_CONSOLE)
    {
        for (int i = 0; i < GetMaxClients(); i++)
        {
            const Client& client = GetClientByIndex(i);

            if (client.IsPlayer())
                client.Print(PRINT_CONSOLE, buffer);
        }
    }
    else
    {
        cstrcat(buffer, "\n");

        g_engfuncs.pfnMessageBegin(MSG_BROADCAST, g_netMsg->GetId(NETMSG_TEXTMSG), nullptr, nullptr);
        g_engfuncs.pfnWriteByte(printType == PRINT_CENTER ? 4 : 3);
        g_engfuncs.pfnWriteString(buffer);
        g_engfuncs.pfnMessageEnd();
    }
}

#pragma warning (disable : 4172)
const Entity& Engine::GetEntityByIndex(int index)
{
    return g_engfuncs.pfnPEntityOfEntIndex(index);
}
#pragma warning (default : 4172)

const Client& Engine::GetClientByIndex(int index)
{
    return m_clients[index];
}

void Engine::MaintainClients(void)
{
    for (const auto& client : g_clients)
    {
        if (client.index < 0)
            continue;

        if (FNullEnt(client.ent))
            continue;

        m_clients[client.index].Maintain(g_engfuncs.pfnPEntityOfEntIndex(client.index));
    }
}

void Engine::DrawLine(const Client& client, const Vector& start, const Vector& end, const Color& color, int width, int noise, int speed, int life, int lineType)
{
    if (!g_sendMessage)
        return;

    if (!client.IsValid())
        return;

    g_engfuncs.pfnMessageBegin(MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, nullptr, client);
    g_engfuncs.pfnWriteByte(TE_BEAMPOINTS);

    g_engfuncs.pfnWriteCoord(start.x);
    g_engfuncs.pfnWriteCoord(start.y);
    g_engfuncs.pfnWriteCoord(start.z);

    g_engfuncs.pfnWriteCoord(end.x);
    g_engfuncs.pfnWriteCoord(end.y);
    g_engfuncs.pfnWriteCoord(end.z);

    switch (lineType)
    {
    case LINE_SIMPLE:
        g_engfuncs.pfnWriteShort(g_modelIndexLaser);
        break;

    case LINE_ARROW:
        g_engfuncs.pfnWriteShort(g_modelIndexArrow);
        break;
    }

    g_engfuncs.pfnWriteByte(0);
    g_engfuncs.pfnWriteByte(10);

    g_engfuncs.pfnWriteByte(life);
    g_engfuncs.pfnWriteByte(width);
    g_engfuncs.pfnWriteByte(noise);

    g_engfuncs.pfnWriteByte(color.red);
    g_engfuncs.pfnWriteByte(color.green);
    g_engfuncs.pfnWriteByte(color.blue);

    g_engfuncs.pfnWriteByte(color.alpha); // alpha as brightness here
    g_engfuncs.pfnWriteByte(speed);

    g_engfuncs.pfnMessageEnd();
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// CLIENT
//////////////////////////////////////////////////////////////////////////
float Client::GetShootingConeDeviation(const Vector& pos) const
{
    engine->BuildGlobalVectors(GetViewAngles());

    return g_pGlobals->v_forward | (pos - GetHeadOrigin()).Normalize();
}

bool Client::IsInViewCone(const Vector& pos) const
{
    engine->BuildGlobalVectors(GetViewAngles());
    return ((pos - GetHeadOrigin()).Normalize() | g_pGlobals->v_forward) >= ccosf(Math::DegreeToRadian((GetFOV() > 0.0f ? GetFOV() : 90.0f) * 0.5f));
}

bool Client::IsVisible(const Vector& pos) const
{
    Tracer trace(GetHeadOrigin(), pos, NO_BOTH, m_ent);

    return !(trace.Fire() != 1.0);
}

bool Client::HasFlag(int clientFlags)
{
    return (m_flags & clientFlags) == clientFlags;
}

Vector Client::GetOrigin(void) const
{
    return m_safeOrigin;
}

bool Client::IsAlive(void) const
{
    return !!(m_flags & CLIENT_ALIVE | CLIENT_VALID);
}

void Client::Maintain(const Entity& ent)
{
    if (ent.IsPlayer())
    {
        m_ent = ent;

        m_safeOrigin = ent.GetOrigin();
        m_flags |= ent.IsAlive() ? CLIENT_VALID | CLIENT_ALIVE : CLIENT_VALID;
    }
    else
    {
        m_safeOrigin = nullvec;
        m_flags = ~(CLIENT_VALID | CLIENT_ALIVE);
    }
}