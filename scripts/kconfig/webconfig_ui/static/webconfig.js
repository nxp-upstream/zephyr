let menuTree = [];
let selectedSymbol = null;
let configChanged = false;
let heartbeatInterval = null;

// 全局变量
let currentTab = 'tree';
let sessionChangesData = [];
let allChangesData = [];

function switchTab(tabName) {
    currentTab = tabName;

    // Update tab buttons
    document.querySelectorAll('.tab-button').forEach(btn => {
        btn.classList.remove('active');
        if (btn.getAttribute('data-tab') === tabName) {
            btn.classList.add('active');
        }
    });

    // Update tab content
    document.querySelectorAll('.tab-content').forEach(content => {
        content.classList.remove('active');
    });
    document.getElementById(`tab-${tabName}`).classList.add('active');

    if (tabName === 'session-changes') {
        loadChanges('session');
    } else if (tabName === 'all-changes') {
        loadChanges('all');
    }
}

function loadChanges(mode = 'session') {
    fetch(`/api/changes?mode=${mode}`)
        .then(response => response.json())
        .then(data => {
            if (mode === 'session') {
                sessionChangesData = data.changes;
                document.getElementById('session-changes-count').textContent = data.count;
                renderChanges('session', sessionChangesData, 'session-changes-list');
            } else {
                allChangesData = data.changes;
                document.getElementById('all-changes-count').textContent = data.count;
                renderChanges('all', allChangesData, 'all-changes-list');
            }
        })
        .catch(error => {
            console.error('Error loading changes:', error);
            showStatus('Error loading changes', 'error');
        });
}

function renderChanges(mode, changesData, containerId) {
    const container = document.getElementById(containerId);

    if (changesData.length === 0) {
        container.innerHTML = `<div class="no-changes">No ${mode === 'session' ? 'session' : ''} changes</div>`;
        return;
    }

    let html = '<div class="changes-header">';
    html += `<div class="changes-title">${mode === 'session' ? 'Session' : 'All'} Changes</div>`;
    html += '<div class="changes-actions">';
    html += `<button class="btn-small" onclick="exportChanges('${mode}')">Export</button>`;
    html += `<button class="btn-small btn-danger" onclick="resetAllChanges('${mode}')">Reset All</button>`;
    html += '</div>';
    html += '</div>';

    html += '<div class="changes-items">';

    changesData.forEach(change => {
        html += '<div class="change-item">';
        html += '<div class="change-header">';
        html += `<span class="change-name">${change.name}</span>`;
        if (change.prompt) {
            html += `<span class="change-prompt">${change.prompt}</span>`;
        }
        html += '</div>';

        html += '<div class="change-values">';
        html += `<span class="change-type">${change.type}</span>`;

        if (mode === 'session') {
            html += `<span class="change-arrow">${change.original} → ${change.value}</span>`;
        } else {
            if (change.default !== null && change.default !== undefined) {
                html += `<span class="change-arrow">${change.default} → ${change.value}</span>`;
            } else if (change.is_new) {
                html += `<span class="change-new">NEW</span>`;
                html += `<span class="change-value">${change.value}</span>`;
            } else {
                html += `<span class="change-value">${change.value}</span>`;
            }
        }

        html += `<button class="btn-reset" onclick="resetSymbol('${change.name}', '${mode}')">↺</button>`;
        html += '</div>';
        html += '</div>';
    });

    html += '</div>';
    container.innerHTML = html;
}

function resetSymbol(symbolName, mode) {
    const resetType = mode === 'session' ? 'session' : 'default';
    if (!confirm(`Reset ${symbolName} to ${resetType === 'session' ? 'session start' : 'default'} value?`)) {
        return;
    }

    fetch('/api/reset_symbol', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            symbol: symbolName,
            type: resetType
        })
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showStatus(`${symbolName} reset`, 'success');
            loadChanges('session');
            loadChanges('all');
            if (currentTab === 'tree') {
                loadMenuTree(document.getElementById('show-all').checked);
            }
            document.getElementById('session-changes-count').textContent = data.session_changes || 0;
        }
    });
}

function resetAllChanges() {
    if (!confirm('Reset all changes to default values?')) {
        return;
    }

    const resetPromises = changesData.map(change =>
        fetch('/api/reset_symbol', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ symbol: change.name })
        })
    );

    Promise.all(resetPromises)
        .then(() => {
            showStatus('All changes reset', 'success');
            loadChanges();
            loadMenuTree(document.getElementById('show-all').checked);
        })
        .catch(error => {
            console.error('Error resetting changes:', error);
            showStatus('Error resetting changes', 'error');
        });
}

// 修改 exportChanges 函数
function exportChanges() {
    let text = 'Session Configuration Changes\n';
    text += '==============================\n\n';
    text += `Date: ${new Date().toLocaleString()}\n\n`;

    changesData.forEach(change => {
        text += `${change.name}:\n`;
        if (change.prompt) {
            text += `  Description: ${change.prompt}\n`;
        }
        text += `  Type: ${change.type}\n`;
        text += `  Changed: ${change.original} → ${change.value}\n`;
        text += '\n';
    });

    // Create download
    const blob = new Blob([text], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `session_changes_${new Date().getTime()}.txt`;
    a.click();
    URL.revokeObjectURL(url);
}

// 更新计数
function updateChangeCounts() {
    // 更新会话改动计数
    fetch('/api/changes?mode=session')
        .then(response => response.json())
        .then(data => {
            document.getElementById('session-changes-count').textContent = data.count;
        });

    // 更新所有改动计数
    fetch('/api/changes?mode=all')
        .then(response => response.json())
        .then(data => {
            document.getElementById('all-changes-count').textContent = data.count;
        });
}

// 修改 updateSymbolValue 函数
const originalUpdateSymbolValue = updateSymbolValue;
updateSymbolValue = function() {
    originalUpdateSymbolValue();
    setTimeout(updateChangeCounts, 500);
};

// 页面加载时初始化
document.addEventListener('DOMContentLoaded', function() {
    loadMenuTree();
    startHeartbeat();
    updateChangeCounts();
});

function startHeartbeat() {
    // 每5秒发送一次心跳
    heartbeatInterval = setInterval(() => {
        fetch('/api/heartbeat', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            }
        }).catch(() => {
            // 服务器已关闭
            clearInterval(heartbeatInterval);
            showStatus('Server connection lost', 'error');
        });
    }, 5000);
}

function saveAndExit() {
    if (!confirm('Save configuration and exit?')) {
        return;
    }

    fetch('/api/save_and_exit', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({})
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            clearInterval(heartbeatInterval);
            showStatus('Configuration saved. Closing...', 'success');
            setTimeout(() => {
                window.close();
                // 如果window.close()不工作，显示消息
                showStatus('Configuration saved. You can close this window.', 'success');
            }, 1000);
        } else {
            showStatus('Error: ' + data.error, 'error');
        }
    })
    .catch(error => {
        console.error('Error:', error);
        showStatus('Error saving and exiting', 'error');
    });
}

// 页面卸载时清理
window.addEventListener('beforeunload', function(e) {
    if (heartbeatInterval) {
        clearInterval(heartbeatInterval);
    }

    if (configChanged) {
        e.preventDefault();
        e.returnValue = 'You have unsaved changes. Are you sure you want to leave?';
    }
});

function loadMenuTree(showAll = false) {
    fetch(`/api/menu_tree?show_all=${showAll}`)
        .then(response => response.json())
        .then(data => {
            menuTree = data;
            renderMenuTree();
        })
        .catch(error => {
            console.error('Error loading menu tree:', error);
            showStatus('Error loading configuration', 'error');
        });
}

function renderMenuTree() {
    const container = document.getElementById('menu-tree');
    container.innerHTML = '';
    renderTreeItems(menuTree, container, 0);
}

function renderTreeItems(items, container, level) {
    items.forEach(item => {
        const itemDiv = document.createElement('div');
        itemDiv.className = 'tree-item';
        itemDiv.style.paddingLeft = `${level * 20}px`;

        const contentDiv = document.createElement('div');
        contentDiv.className = 'tree-item-content';

        // Add toggle for menus with children
        if (item.children && item.children.length > 0) {
            const toggle = document.createElement('span');
            toggle.className = 'tree-toggle';
            toggle.innerHTML = '▶';
            toggle.onclick = (e) => {
                e.stopPropagation();
                toggleTreeItem(itemDiv, item);
            };
            contentDiv.appendChild(toggle);
        } else {
            const spacer = document.createElement('span');
            spacer.style.width = '16px';
            spacer.style.display = 'inline-block';
            contentDiv.appendChild(spacer);
        }

        // Add item name
        const nameSpan = document.createElement('span');
        nameSpan.textContent = item.prompt || item.name;
        if (!item.visible) {
            nameSpan.className = 'invisible';
        }
        contentDiv.appendChild(nameSpan);

        // Add value for symbols
        if (item.type === 'symbol' && item.value !== undefined) {
            const valueSpan = document.createElement('span');
            valueSpan.className = 'symbol-value';

            if (item.symbol_type === 'bool') {
                valueSpan.textContent = item.value;
                valueSpan.classList.add(`bool-${item.value}`);
            } else if (item.symbol_type === 'tristate') {
                valueSpan.textContent = item.value;
                if (item.value === 'm') {
                    valueSpan.classList.add('tristate-m');
                } else {
                    valueSpan.classList.add(`bool-${item.value}`);
                }
            } else {
                valueSpan.textContent = item.value;
            }

            contentDiv.appendChild(valueSpan);
        }

        // Add NEW indicator
        if (item.is_new) {
            const newSpan = document.createElement('span');
            newSpan.className = 'symbol-new';
            newSpan.textContent = '(NEW)';
            contentDiv.appendChild(newSpan);
        }

        contentDiv.onclick = () => selectItem(item);

        itemDiv.appendChild(contentDiv);

        // Add children container
        if (item.children && item.children.length > 0) {
            const childrenDiv = document.createElement('div');
            childrenDiv.className = 'tree-children';
            childrenDiv.style.display = 'none';
            renderTreeItems(item.children, childrenDiv, level + 1);
            itemDiv.appendChild(childrenDiv);
        }

        container.appendChild(itemDiv);
    });
}

function toggleTreeItem(itemDiv, item) {
    const toggle = itemDiv.querySelector('.tree-toggle');
    const children = itemDiv.querySelector('.tree-children');

    if (children.style.display === 'none') {
        children.style.display = 'block';
        toggle.innerHTML = '▼';
    } else {
        children.style.display = 'none';
        toggle.innerHTML = '▶';
    }
}

function selectItem(item) {
    // Clear previous selection
    document.querySelectorAll('.tree-item').forEach(el => {
        el.classList.remove('selected');
    });

    // Highlight selected item
    event.currentTarget.parentElement.classList.add('selected');

    selectedSymbol = item;

    if (item.type === 'symbol') {
        showSymbolDetails(item);
    } else {
        showItemInfo(item);
    }
}

function showSymbolDetails(symbol) {
    fetch(`/api/symbol/${symbol.name}`)
        .then(response => response.json())
        .then(data => {
            const detailsDiv = document.getElementById('symbol-details');

            let html = `<h2>${data.name}</h2>`;

            // Value editor
            html += '<div class="detail-section">';
            html += '<div class="detail-label">Value:</div>';
            html += '<div class="value-editor">';

            if (data.type === 'bool' || data.type === 'tristate') {
                html += '<select id="value-select">';
                if (symbol.assignable) {
                    symbol.assignable.forEach(val => {
                        const strVal = ['n', 'm', 'y'][val];
                        html += `<option value="${strVal}" ${symbol.value === strVal ? 'selected' : ''}>${strVal}</option>`;
                    });
                }
                html += '</select>';
                html += '<button onclick="updateSymbolValue()">Set</button>';
            } else {
                html += `<input type="text" id="value-input" value="${symbol.value}">`;
                html += '<button onclick="updateSymbolValue()">Set</button>';
            }

            html += '</div>';
            html += '</div>';

            // Type
            html += '<div class="detail-section">';
            html += `<div class="detail-label">Type:</div>`;
            html += `<div class="detail-value">${data.type}</div>`;
            html += '</div>';

            // Dependencies
            if (data.depends_on) {
                html += '<div class="detail-section">';
                html += '<div class="detail-label">Depends on:</div>';
                html += `<div class="detail-value">${data.depends_on}</div>`;
                html += '</div>';
            }

            // Defaults
            if (data.defaults && data.defaults.length > 0) {
                html += '<div class="detail-section">';
                html += '<div class="detail-label">Defaults:</div>';
                data.defaults.forEach(def => {
                    html += `<div class="detail-value">${def.value}`;
                    if (def.condition) {
                        html += ` (if ${def.condition})`;
                    }
                    html += '</div>';
                });
                html += '</div>';
            }

            // Help text
            if (data.help) {
                html += '<div class="detail-section">';
                html += '<div class="detail-label">Help:</div>';
                html += `<div class="detail-value help-text">${data.help}</div>`;
                html += '</div>';
            }

            detailsDiv.innerHTML = html;
        })
        .catch(error => {
            console.error('Error loading symbol details:', error);
        });
}

function showItemInfo(item) {
    const detailsDiv = document.getElementById('symbol-details');

    let html = `<h2>${item.prompt || item.name}</h2>`;
    html += `<div class="detail-section">`;
    html += `<div class="detail-label">Type:</div>`;
    html += `<div class="detail-value">${item.type}</div>`;
    html += `</div>`;

    if (item.help) {
        html += '<div class="detail-section">';
        html += '<div class="detail-label">Help:</div>';
        html += `<div class="detail-value help-text">${item.help}</div>`;
        html += '</div>';
    }

    detailsDiv.innerHTML = html;
}

function updateSymbolValue() {
    if (!selectedSymbol) return;

    let value;
    const selectEl = document.getElementById('value-select');
    const inputEl = document.getElementById('value-input');

    if (selectEl) {
        value = selectEl.value;
    } else if (inputEl) {
        value = inputEl.value;
    } else {
        return;
    }

    fetch('/api/set_value', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            symbol: selectedSymbol.name,
            value: value
        })
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            configChanged = data.changed;
            showStatus('Value updated', 'success');
            loadMenuTree(document.getElementById('show-all').checked);
        } else {
            showStatus('Error: ' + data.error, 'error');
        }
    })
    .catch(error => {
        console.error('Error updating value:', error);
        showStatus('Error updating value', 'error');
    });
}

function saveConfig() {
    fetch('/api/save_config', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({})
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            configChanged = false;
            showStatus(`Configuration saved to ${data.filename}`, 'success');
        } else {
            showStatus('Error: ' + data.error, 'error');
        }
    })
    .catch(error => {
        console.error('Error saving config:', error);
        showStatus('Error saving configuration', 'error');
    });
}

function saveAsConfig() {
    const filename = prompt('Enter filename:', '.config');
    if (!filename) return;

    fetch('/api/save_config', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ filename: filename })
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            configChanged = false;
            showStatus(`Configuration saved to ${data.filename}`, 'success');
        } else {
            showStatus('Error: ' + data.error, 'error');
        }
    })
    .catch(error => {
        console.error('Error saving config:', error);
        showStatus('Error saving configuration', 'error');
    });
}

function loadConfig() {
    if (configChanged) {
        if (!confirm('You have unsaved changes. Load new configuration anyway?')) {
            return;
        }
    }

    const filename = prompt('Enter filename to load:', '.config');
    if (!filename) return;

    fetch('/api/load_config', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ filename: filename })
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            configChanged = false;
            showStatus(`Configuration loaded from ${data
.filename}`, 'success');
            loadMenuTree(document.getElementById('show-all').checked);
        } else {
            showStatus('Error: ' + data.error, 'error');
        }
    })
    .catch(error => {
        console.error('Error loading config:', error);
        showStatus('Error loading configuration', 'error');
    });
}

function toggleShowAll() {
    const showAll = document.getElementById('show-all').checked;
    loadMenuTree(showAll);
}

function searchSymbols() {
    const query = document.getElementById('search').value;

    if (query.length < 2) {
        // Clear search results if query is too short
        if (query.length === 0) {
            loadMenuTree(document.getElementById('show-all').checked);
        }
        return;
    }

    fetch(`/api/search?q=${encodeURIComponent(query)}`)
        .then(response => response.json())
        .then(results => {
            displaySearchResults(results);
        })
        .catch(error => {
            console.error('Error searching:', error);
        });
}

function displaySearchResults(results) {
    const container = document.getElementById('menu-tree');
    container.innerHTML = '';

    if (results.length === 0) {
        container.innerHTML = '<p>No results found</p>';
        return;
    }

    const heading = document.createElement('h3');
    heading.textContent = `Search Results (${results.length})`;
    container.appendChild(heading);

    results.forEach(result => {
        const itemDiv = document.createElement('div');
        itemDiv.className = 'tree-item';

        const contentDiv = document.createElement('div');
        contentDiv.className = 'tree-item-content';

        const nameSpan = document.createElement('span');
        nameSpan.textContent = result.prompt || result.name;
        contentDiv.appendChild(nameSpan);

        const valueSpan = document.createElement('span');
        valueSpan.className = 'symbol-value';
        valueSpan.textContent = result.value;
        contentDiv.appendChild(valueSpan);

        contentDiv.onclick = () => {
            selectedSymbol = {
                name: result.name,
                type: 'symbol',
                value: result.value
            };
            showSymbolDetails(selectedSymbol);
        };

        itemDiv.appendChild(contentDiv);
        container.appendChild(itemDiv);
    });
}

function showStatus(message, type = 'info') {
    const statusBar = document.getElementById('status-bar');
    statusBar.textContent = message;
    statusBar.className = `status-${type}`;

    // Clear status after 5 seconds
    setTimeout(() => {
        if (statusBar.textContent === message) {
            statusBar.textContent = configChanged ? 'Modified' : '';
        }
    }, 5000);
}

// Handle keyboard shortcuts
document.addEventListener('keydown', function(e) {
    if (e.ctrlKey || e.metaKey) {
        switch(e.key) {
            case 's':
                e.preventDefault();
                saveConfig();
                break;
            case 'o':
                e.preventDefault();
                loadConfig();
                break;
            case 'f':
                e.preventDefault();
                document.getElementById('search').focus();
                break;
        }
    }
});

// Warn before leaving if there are unsaved changes
window.addEventListener('beforeunload', function(e) {
    if (configChanged) {
        e.preventDefault();
        e.returnValue = 'You have unsaved changes. Are you sure you want to leave?';
    }
});
