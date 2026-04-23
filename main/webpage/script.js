const API =
{
    send: "/api/can/send",
    config: "/api/can/config",
    receive: "/api/can/rx",
    status: "/api/can/status"
};

const state =
{
    txCount: 0,
    rxCount: 0,
    errCount: 0,
    log: [],
    maxLogEntries: 300,
    pollHandle: null,
    pollIntervalMs: 500,
    canState: "stopped",
    lastFrameSignature: ""
};

const CAN_STATE_INFO =
{
    "running":
    {
        label: "Running",
        className: "state-running",
        description: "Controlador ativo e apto a transmitir e receber frames."
    },
    "stopped":
    {
        label: "Stopped",
        className: "state-stopped",
        description: "Controlador parado ou ainda não inicializado."
    },
    "bus-off":
    {
        label: "Bus-Off",
        className: "state-bus-off",
        description: "Controlador isolado do barramento após excesso de erros."
    },
    "recovering":
    {
        label: "Recovering",
        className: "state-recovering",
        description: "Controlador tentando se recuperar e voltar ao estado operacional."
    },
    "unknown":
    {
        label: "Unknown",
        className: "state-unknown",
        description: "Estado do barramento ainda não informado pelo firmware."
    }
};

const el = {};

document.addEventListener("DOMContentLoaded", () =>
{
    mapElements();
    populateDlc();
    buildDataInputs();
    bindEvents();
    syncFormState();
    updateStats();
    setCanState("stopped");
    startPolling();
});

function mapElements()
{
    el.canId = document.getElementById("canId");
    el.idHint = document.getElementById("idHint");
    el.frameFormat = document.getElementById("frameFormat");
    el.frameType = document.getElementById("frameType");
    el.dlc = document.getElementById("dlc");
    el.dataGrid = document.getElementById("dataGrid");
    el.txPreview = document.getElementById("txPreview");
    el.clearTxBtn = document.getElementById("clearTxBtn");
    el.sendBtn = document.getElementById("sendBtn");

    el.baudrate = document.getElementById("baudrate");
    el.operationMode = document.getElementById("operationMode");
    el.filterId = document.getElementById("filterId");
    el.maskId = document.getElementById("maskId");
    el.applyConfigBtn = document.getElementById("applyConfigBtn");

    el.connectionStatus = document.getElementById("connectionStatus");
    el.canStateBadge = document.getElementById("canStateBadge");
    el.canStateText = document.getElementById("canStateText");
    el.canStateDescription = document.getElementById("canStateDescription");

    el.statTx = document.getElementById("statTx");
    el.statRx = document.getElementById("statRx");
    el.statErr = document.getElementById("statErr");
    el.statShown = document.getElementById("statShown");

    el.logFilter = document.getElementById("logFilter");
    el.autoScroll = document.getElementById("autoScroll");
    el.clearLogBtn = document.getElementById("clearLogBtn");
    el.lastFrame = document.getElementById("lastFrame");
    el.rxLog = document.getElementById("rxLog");
    el.toast = document.getElementById("toast");
}

function populateDlc()
{
    for (let i = 0; i <= 8; i++)
    {
        const option = document.createElement("option");
        option.value = String(i);
        option.textContent = `${i} byte${i === 1 ? "" : "s"}`;
        if (i === 8)
        {
            option.selected = true;
        }
        el.dlc.appendChild(option);
    }
}

function buildDataInputs()
{
    el.dataGrid.innerHTML = "";

    for (let i = 0; i < 8; i++)
    {
        const wrapper = document.createElement("div");
        wrapper.className = "byte-input-wrap";

        const label = document.createElement("span");
        label.className = "byte-label";
        label.textContent = `BYTE ${i}`;

        const input = document.createElement("input");
        input.className = "byte-input";
        input.type = "text";
        input.maxLength = 2;
        input.placeholder = "00";
        input.dataset.index = String(i);

        input.addEventListener("input", () =>
        {
            input.value = sanitizeHex(input.value, 2);
            updatePreview();
        });

        wrapper.appendChild(label);
        wrapper.appendChild(input);
        el.dataGrid.appendChild(wrapper);
    }
}

function bindEvents()
{
    el.canId.addEventListener("input", () =>
    {
        const maxLen = el.frameFormat.value === "extended" ? 8 : 3;
        el.canId.value = sanitizeHex(el.canId.value, maxLen);
        updatePreview();
    });

    el.frameFormat.addEventListener("change", syncFormState);
    el.frameType.addEventListener("change", syncFormState);
    el.dlc.addEventListener("change", syncFormState);

    el.clearTxBtn.addEventListener("click", clearTxForm);
    el.sendBtn.addEventListener("click", sendFrame);
    el.applyConfigBtn.addEventListener("click", applyConfig);

    el.filterId.addEventListener("input", () =>
    {
        el.filterId.value = sanitizeHex(el.filterId.value, 8);
    });

    el.maskId.addEventListener("input", () =>
    {
        el.maskId.value = sanitizeHex(el.maskId.value, 8);
    });

    el.logFilter.addEventListener("input", renderLog);
    el.clearLogBtn.addEventListener("click", clearLog);
}

function syncFormState()
{
    const isExtended = el.frameFormat.value === "extended";
    const isRemote = el.frameType.value === "remote";
    const dlc = Number(el.dlc.value);

    el.canId.maxLength = isExtended ? 8 : 3;
    el.canId.value = sanitizeHex(el.canId.value, el.canId.maxLength);
    el.idHint.textContent = isExtended
        ? "Frame estendido: até 8 dígitos hex."
        : "Frame padrão: até 3 dígitos hex.";

    [...document.querySelectorAll(".byte-input")].forEach((input, index) =>
    {
        input.disabled = isRemote || index >= dlc;

        if (input.disabled)
        {
            input.value = "";
        }
    });

    updatePreview();
}

function sanitizeHex(value, maxLen)
{
    return value.replace(/[^0-9a-fA-F]/g, "").toUpperCase().slice(0, maxLen);
}

function normalizeCanState(rawState)
{
    if (rawState == null)
    {
        return "unknown";
    }

    const text = String(rawState)
        .trim()
        .toLowerCase()
        .replace(/_/g, "-")
        .replace(/\s+/g, "-");

    if (text === "busoff")
    {
        return "bus-off";
    }

    if (CAN_STATE_INFO[text])
    {
        return text;
    }

    return "unknown";
}

function setConnectionStatus(isOnline)
{
    el.connectionStatus.textContent = isOnline ? "Conectado" : "Desconectado";
    el.connectionStatus.classList.toggle("online", isOnline);
    el.connectionStatus.classList.toggle("offline", !isOnline);
}

function setCanState(rawState)
{
    const normalized = normalizeCanState(rawState);
    const info = CAN_STATE_INFO[normalized] || CAN_STATE_INFO.unknown;

    state.canState = normalized;

    el.canStateBadge.className = `state-badge ${info.className}`;
    el.canStateBadge.textContent = info.label;
    el.canStateText.textContent = info.label;
    el.canStateDescription.textContent = info.description;
}

function collectDataBytes()
{
    const dlc = Number(el.dlc.value);
    const bytes = [];
    const inputs = [...document.querySelectorAll(".byte-input")];

    for (let i = 0; i < dlc; i++)
    {
        const value = inputs[i].value.trim();
        bytes.push(value === "" ? "00" : value.padStart(2, "0"));
    }

    return bytes;
}

function updatePreview()
{
    const formatLabel = el.frameFormat.value === "extended" ? "EXT" : "STD";
    const typeLabel = el.frameType.value === "remote" ? "RTR" : "DATA";
    const idText = el.canId.value || "---";
    const dataText = typeLabel === "RTR" ? "<sem dados>" : collectDataBytes().join(" ") || "--";

    el.txPreview.textContent =
        `ID: ${idText} | ${formatLabel} | ${typeLabel}\n` +
        `DLC: ${el.dlc.value}\n` +
        `DATA: ${dataText}`;
}

function clearTxForm()
{
    el.canId.value = "";
    el.frameFormat.value = "standard";
    el.frameType.value = "data";
    el.dlc.value = "8";

    [...document.querySelectorAll(".byte-input")].forEach((input) =>
    {
        input.value = "";
    });

    syncFormState();
    showToast("Campos limpos.", "success");
}

function validateFrame()
{
    const id = el.canId.value.trim();
    const isExtended = el.frameFormat.value === "extended";

    if (id === "")
    {
        return { ok: false, message: "Preencha o CAN ID." };
    }

    if (!isExtended && id.length > 3)
    {
        return { ok: false, message: "ID standard aceita até 3 dígitos hex." };
    }

    const idValue = parseInt(id, 16);

    if (Number.isNaN(idValue))
    {
        return { ok: false, message: "CAN ID inválido." };
    }

    if (!isExtended && idValue > 0x7FF)
    {
        return { ok: false, message: "ID standard deve estar entre 0x000 e 0x7FF." };
    }

    if (isExtended && idValue > 0x1FFFFFFF)
    {
        return { ok: false, message: "ID extended deve estar entre 0x00000000 e 0x1FFFFFFF." };
    }

    return { ok: true };
}

async function sendFrame()
{
    const validation = validateFrame();

    if (!validation.ok)
    {
        showToast(validation.message, "warn");
        return;
    }

    const payload =
    {
        id: parseInt(el.canId.value, 16),
        id_hex: el.canId.value,
        format: el.frameFormat.value,
        type: el.frameType.value,
        dlc: Number(el.dlc.value),
        data: el.frameType.value === "remote"
            ? []
            : collectDataBytes().map((item) => parseInt(item, 16))
    };

    lockButton(el.sendBtn, true, "Enviando...");

    try
    {
        const response = await fetch(API.send,
        {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });

        if (!response.ok)
        {
            throw new Error(`HTTP ${response.status}`);
        }

        state.txCount += 1;
        updateStats();
        showToast(`Frame ${el.canId.value} enviado.`, "success");
    }
    catch (error)
    {
        state.errCount += 1;
        updateStats();
        showToast(`Falha ao enviar frame (${error.message}).`, "error");
    }
    finally
    {
        lockButton(el.sendBtn, false, "Enviar");
    }
}

async function applyConfig()
{
    const payload =
    {
        baudrate: Number(el.baudrate.value),
        mode: el.operationMode.value,
        filter: el.filterId.value || null,
        mask: el.maskId.value || null
    };

    lockButton(el.applyConfigBtn, true, "Aplicando...");

    try
    {
        const response = await fetch(API.config,
        {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });

        if (!response.ok)
        {
            throw new Error(`HTTP ${response.status}`);
        }

        showToast("Configuração aplicada.", "success");
    }
    catch (error)
    {
        state.errCount += 1;
        updateStats();
        showToast(`Falha ao aplicar configuração (${error.message}).`, "error");
    }
    finally
    {
        lockButton(el.applyConfigBtn, false, "Aplicar configuração");
    }
}

function lockButton(button, shouldLock, label)
{
    button.disabled = shouldLock;
    button.textContent = label;
}

function startPolling()
{
    if (state.pollHandle)
    {
        clearInterval(state.pollHandle);
    }

    pollCycle();
    state.pollHandle = setInterval(pollCycle, state.pollIntervalMs);
}

async function pollCycle()
{
    const [rxResult, statusResult] = await Promise.allSettled(
    [
        fetchRxData(),
        fetchStatusData()
    ]);

    const anySuccess = rxResult.status === "fulfilled" || statusResult.status === "fulfilled";
    setConnectionStatus(anySuccess);

    if (rxResult.status === "fulfilled")
    {
        ingestRxPayload(rxResult.value);
    }

    if (statusResult.status === "fulfilled")
    {
        ingestStatusPayload(statusResult.value);
    }
}

async function fetchRxData()
{
    const response = await fetch(API.receive, { cache: "no-store" });

    if (!response.ok)
    {
        throw new Error(`HTTP ${response.status}`);
    }

    return response.json();
}

async function fetchStatusData()
{
    const response = await fetch(API.status, { cache: "no-store" });

    if (!response.ok)
    {
        throw new Error(`HTTP ${response.status}`);
    }

    return response.json();
}

function ingestRxPayload(payload)
{
    if (!payload)
    {
        return;
    }

    if (payload.can_state != null || payload.canState != null || payload.state != null)
    {
        setCanState(payload.can_state ?? payload.canState ?? payload.state);
    }

    if (Array.isArray(payload.frames))
    {
        payload.frames.forEach((frame) => addRxFrame(frame));
        return;
    }

    if (payload.frame)
    {
        addRxFrame(payload.frame);
        return;
    }

    if (isFrameLike(payload))
    {
        addRxFrame(payload);
    }
}

function ingestStatusPayload(payload)
{
    if (!payload)
    {
        return;
    }

    if (payload.can_state != null || payload.canState != null || payload.state != null)
    {
        setCanState(payload.can_state ?? payload.canState ?? payload.state);
    }

    if (payload.mode != null)
    {
        const normalizedMode = String(payload.mode).toLowerCase();
        if ([...el.operationMode.options].some((option) => option.value === normalizedMode))
        {
            el.operationMode.value = normalizedMode;
        }
    }

    if (payload.baudrate != null && !Number.isNaN(Number(payload.baudrate)))
    {
        el.baudrate.value = String(payload.baudrate);
    }

    if (payload.filter != null)
    {
        el.filterId.value = sanitizeHex(String(payload.filter), 8);
    }

    if (payload.mask != null)
    {
        el.maskId.value = sanitizeHex(String(payload.mask), 8);
    }
}

function isFrameLike(value)
{
    return value && typeof value === "object" && (
        "id" in value ||
        "id_hex" in value ||
        "data" in value ||
        "dlc" in value
    );
}

function addRxFrame(frame)
{
    if (!frame || !isFrameLike(frame))
    {
        return;
    }

    const normalized = normalizeFrame(frame);
    const signature = JSON.stringify(normalized);

    if (signature === state.lastFrameSignature)
    {
        return;
    }

    state.lastFrameSignature = signature;
    state.rxCount += 1;

    if (normalized.err)
    {
        state.errCount += 1;
    }

    state.log.unshift(normalized);

    if (state.log.length > state.maxLogEntries)
    {
        state.log.length = state.maxLogEntries;
    }

    updateLastFrame(normalized);
    updateStats();
    renderLog();
}

function normalizeFrame(frame)
{
    const ext = Boolean(frame.ext ?? (frame.format === "extended"));
    const remote = Boolean(frame.rtr ?? (frame.type === "remote"));
    const idValue = frame.id != null
        ? Number(frame.id)
        : parseInt(String(frame.id_hex || frame.identifier || "0"), 16);

    const idHex = Number.isFinite(idValue)
        ? idValue.toString(16).toUpperCase().padStart(ext ? 8 : 3, "0")
        : "---";

    const dataArray = Array.isArray(frame.data)
        ? frame.data
        : [];

    const dataHex = remote
        ? []
        : dataArray.map((item) =>
        {
            if (typeof item === "number")
            {
                return item.toString(16).toUpperCase().padStart(2, "0");
            }

            return sanitizeHex(String(item), 2).padStart(2, "0");
        });

    return {
        timestamp: frame.timestamp || getCurrentTimestamp(),
        id: idHex,
        format: ext ? "extended" : "standard",
        type: remote ? "remote" : "data",
        dlc: Number(frame.dlc ?? dataHex.length ?? 0),
        data: dataHex,
        err: Boolean(frame.err),
        state: normalizeCanState(frame.can_state ?? frame.canState ?? frame.state ?? state.canState)
    };
}

function updateLastFrame(frame)
{
    const dataText = frame.type === "remote" ? "<RTR>" : (frame.data.join(" ") || "--");
    const formatText = frame.format === "extended" ? "Extended" : "Standard";
    const typeText = frame.type === "remote" ? "Remote" : "Data";
    const stateText = CAN_STATE_INFO[frame.state]?.label || CAN_STATE_INFO.unknown.label;

    el.lastFrame.classList.remove("empty");
    el.lastFrame.innerHTML = `
        <div class="last-frame-row">
            <span class="last-frame-label">Timestamp</span>
            <span class="last-frame-value">${escapeHtml(frame.timestamp)}</span>
        </div>
        <div class="last-frame-row">
            <span class="last-frame-label">CAN ID</span>
            <span class="last-frame-value">${escapeHtml(frame.id)}</span>
        </div>
        <div class="last-frame-row">
            <span class="last-frame-label">Formato / Tipo</span>
            <span class="last-frame-value">${escapeHtml(formatText)} / ${escapeHtml(typeText)}</span>
        </div>
        <div class="last-frame-row">
            <span class="last-frame-label">DLC</span>
            <span class="last-frame-value">${frame.dlc}</span>
        </div>
        <div class="last-frame-row">
            <span class="last-frame-label">Dados</span>
            <span class="last-frame-value">${escapeHtml(dataText)}</span>
        </div>
        <div class="last-frame-row">
            <span class="last-frame-label">Estado CAN</span>
            <span class="last-frame-value">${escapeHtml(stateText)}</span>
        </div>
    `;
}

function renderLog()
{
    const filter = sanitizeHex(el.logFilter.value || "", 8).toUpperCase();
    const visibleEntries = filter === ""
        ? state.log
        : state.log.filter((entry) => entry.id.includes(filter));

    el.statShown.textContent = String(visibleEntries.length);

    if (visibleEntries.length === 0)
    {
        el.rxLog.innerHTML = `<div class="log-empty">${
            state.log.length === 0
                ? "Nenhum frame recebido ainda."
                : "Nenhum frame corresponde ao filtro."
        }</div>`;
        return;
    }

    el.rxLog.innerHTML = visibleEntries.map((entry) =>
    {
        const flags = [
            entry.format === "extended" ? "EXT" : "STD",
            entry.type === "remote" ? "RTR" : "DATA",
            entry.err ? "ERR" : CAN_STATE_INFO[entry.state]?.label || ""
        ].filter(Boolean).join(" · ");

        const dataText = entry.type === "remote" ? "<RTR>" : (entry.data.join(" ") || "--");

        return `
            <div class="log-entry">
                <span class="log-ts">${escapeHtml(entry.timestamp)}</span>
                <span class="log-id">${escapeHtml(entry.id)}</span>
                <span class="log-dlc">[${entry.dlc}]</span>
                <span class="log-data">${escapeHtml(dataText)}</span>
                <span class="log-flags">${escapeHtml(flags)}</span>
            </div>
        `;
    }).join("");

    if (el.autoScroll.checked)
    {
        el.rxLog.scrollTop = 0;
    }
}

function clearLog()
{
    state.log = [];
    state.rxCount = 0;
    state.lastFrameSignature = "";
    el.lastFrame.classList.add("empty");
    el.lastFrame.textContent = "Aguardando frames do barramento...";
    updateStats();
    renderLog();
    showToast("Log limpo.", "success");
}

function updateStats()
{
    el.statTx.textContent = String(state.txCount);
    el.statRx.textContent = String(state.rxCount);
    el.statErr.textContent = String(state.errCount);
    el.statShown.textContent = String(state.log.length);
}

function getCurrentTimestamp()
{
    const date = new Date();
    return date.toLocaleTimeString("pt-BR", { hour12: false });
}

function showToast(message, type)
{
    el.toast.textContent = message;
    el.toast.className = `toast show ${type}`;

    clearTimeout(showToast._handle);
    showToast._handle = setTimeout(() =>
    {
        el.toast.className = "toast";
    }, 2200);
}

function escapeHtml(value)
{
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

/*
 * Funções globais úteis para o firmware do ESP32.
 * Você pode chamá-las via WebSocket, SSE, polling customizado ou script injetado.
 */
window.updateCanRxFrame = function updateCanRxFrame(frame)
{
    setConnectionStatus(true);
    ingestRxPayload(frame);
};

window.updateCanRxFrames = function updateCanRxFrames(frames)
{
    setConnectionStatus(true);
    ingestRxPayload({ frames });
};

window.updateCanStatus = function updateCanStatus(status)
{
    setConnectionStatus(true);
    ingestStatusPayload(status);
};

window.updateCanSnapshot = function updateCanSnapshot(snapshot)
{
    setConnectionStatus(true);
    ingestStatusPayload(snapshot);
    ingestRxPayload(snapshot);
};
