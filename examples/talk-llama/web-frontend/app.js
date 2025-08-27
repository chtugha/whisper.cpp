class SipClientManager {
    constructor() {
        this.clients = new Map();
        this.serverUrl = 'http://localhost:8081'; // Backend API server
        this.init();
    }
    
    init() {
        this.setupEventListeners();
        this.loadClients();
        this.startStatusUpdates();
    }
    
    setupEventListeners() {
        // Add client form
        document.getElementById('clientForm').addEventListener('submit', (e) => {
            e.preventDefault();
            this.addClient();
        });
        
        // Edit client form
        document.getElementById('editClientForm').addEventListener('submit', (e) => {
            e.preventDefault();
            this.updateClient();
        });
    }
    
    async addClient() {
        const formData = new FormData(document.getElementById('clientForm'));
        const config = {
            client_id: formData.get('clientId'),
            username: formData.get('username'),
            password: formData.get('password'),
            server_ip: formData.get('serverIp'),
            server_port: parseInt(formData.get('serverPort')),
            display_name: formData.get('displayName'),
            auto_answer: formData.get('autoAnswer') === 'true',
            use_tts: formData.get('useTts') === 'true',
            greeting: formData.get('greeting'),
            ai_persona: formData.get('aiPersona')
        };
        
        try {
            const response = await fetch(`${this.serverUrl}/api/clients`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(config)
            });
            
            if (response.ok) {
                this.log(`✅ Added SIP client: ${config.client_id}`);
                document.getElementById('clientForm').reset();
                this.loadClients();
            } else {
                const error = await response.text();
                this.log(`❌ Failed to add client: ${error}`);
            }
        } catch (error) {
            this.log(`❌ Network error: ${error.message}`);
        }
    }
    
    async updateClient() {
        const formData = new FormData(document.getElementById('editClientForm'));
        const clientId = formData.get('clientId');
        const config = {
            client_id: clientId,
            username: formData.get('username'),
            password: formData.get('password'),
            server_ip: formData.get('serverIp'),
            server_port: parseInt(formData.get('serverPort')),
            display_name: formData.get('displayName'),
            greeting: formData.get('greeting')
        };
        
        try {
            const response = await fetch(`${this.serverUrl}/api/clients/${clientId}`, {
                method: 'PUT',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(config)
            });
            
            if (response.ok) {
                this.log(`✅ Updated SIP client: ${clientId}`);
                this.closeEditModal();
                this.loadClients();
            } else {
                const error = await response.text();
                this.log(`❌ Failed to update client: ${error}`);
            }
        } catch (error) {
            this.log(`❌ Network error: ${error.message}`);
        }
    }
    
    async loadClients() {
        try {
            const response = await fetch(`${this.serverUrl}/api/clients`);
            if (response.ok) {
                const clients = await response.json();
                this.renderClients(clients);
                this.updateDashboard(clients);
            } else {
                this.log(`❌ Failed to load clients: ${response.statusText}`);
            }
        } catch (error) {
            this.log(`❌ Network error: ${error.message}`);
            // Show offline mode
            this.renderOfflineMode();
        }
    }
    
    renderClients(clients) {
        const clientList = document.getElementById('clientList');
        
        if (clients.length === 0) {
            clientList.innerHTML = '<p style="text-align: center; color: #86868b; padding: 40px;">No SIP clients configured</p>';
            return;
        }
        
        clientList.innerHTML = clients.map(client => `
            <div class="client-item">
                <div class="client-info">
                    <h4>${client.client_id}</h4>
                    <p>${client.username}@${client.server_ip}:${client.server_port}</p>
                </div>
                <div class="client-status">
                    <div class="status-indicator ${client.status || 'offline'}" title="${client.status || 'offline'}"></div>
                    <div class="client-actions">
                        <button class="btn btn-small btn-success" onclick="sipManager.startClient('${client.client_id}')">Start</button>
                        <button class="btn btn-small btn-secondary" onclick="sipManager.stopClient('${client.client_id}')">Stop</button>
                        <button class="btn btn-small" onclick="sipManager.editClient('${client.client_id}')">Edit</button>
                        <button class="btn btn-small btn-danger" onclick="sipManager.removeClient('${client.client_id}')">Remove</button>
                    </div>
                </div>
            </div>
        `).join('');
    }
    
    renderOfflineMode() {
        const clientList = document.getElementById('clientList');
        clientList.innerHTML = `
            <div style="text-align: center; padding: 40px; color: #86868b;">
                <h3>🔌 Backend Offline</h3>
                <p>Cannot connect to the SIP server backend.</p>
                <p>Make sure the talk-llama server is running.</p>
                <button class="btn" onclick="sipManager.loadClients()" style="margin-top: 15px;">Retry Connection</button>
            </div>
        `;
    }
    
    updateDashboard(clients) {
        document.getElementById('totalClients').textContent = clients.length;
        
        const activeClients = clients.filter(c => c.status === 'registered' || c.status === 'online').length;
        document.getElementById('activeClients').textContent = activeClients;
        
        // TODO: Get actual call count from server
        document.getElementById('activeCalls').textContent = '0';
    }
    
    async startClient(clientId) {
        try {
            const response = await fetch(`${this.serverUrl}/api/clients/${clientId}/start`, {
                method: 'POST'
            });
            
            if (response.ok) {
                this.log(`🚀 Starting SIP client: ${clientId}`);
                setTimeout(() => this.loadClients(), 1000);
            } else {
                this.log(`❌ Failed to start client: ${clientId}`);
            }
        } catch (error) {
            this.log(`❌ Network error: ${error.message}`);
        }
    }
    
    async stopClient(clientId) {
        try {
            const response = await fetch(`${this.serverUrl}/api/clients/${clientId}/stop`, {
                method: 'POST'
            });
            
            if (response.ok) {
                this.log(`⏹️ Stopping SIP client: ${clientId}`);
                setTimeout(() => this.loadClients(), 1000);
            } else {
                this.log(`❌ Failed to stop client: ${clientId}`);
            }
        } catch (error) {
            this.log(`❌ Network error: ${error.message}`);
        }
    }
    
    async removeClient(clientId) {
        if (!confirm(`Are you sure you want to remove SIP client "${clientId}"?`)) {
            return;
        }
        
        try {
            const response = await fetch(`${this.serverUrl}/api/clients/${clientId}`, {
                method: 'DELETE'
            });
            
            if (response.ok) {
                this.log(`🗑️ Removed SIP client: ${clientId}`);
                this.loadClients();
            } else {
                this.log(`❌ Failed to remove client: ${clientId}`);
            }
        } catch (error) {
            this.log(`❌ Network error: ${error.message}`);
        }
    }
    
    editClient(clientId) {
        // TODO: Load client config and populate edit form
        this.log(`📝 Edit client: ${clientId}`);
        document.getElementById('editModal').style.display = 'block';
        document.getElementById('editClientId').value = clientId;
    }
    
    closeEditModal() {
        document.getElementById('editModal').style.display = 'none';
    }
    
    startStatusUpdates() {
        // Update client status every 5 seconds
        setInterval(() => {
            this.loadClients();
        }, 5000);
    }
    
    log(message) {
        const logs = document.getElementById('logs');
        const timestamp = new Date().toLocaleTimeString();
        const logEntry = document.createElement('div');
        logEntry.textContent = `[${timestamp}] ${message}`;
        logs.appendChild(logEntry);
        logs.scrollTop = logs.scrollHeight;
        
        // Keep only last 100 log entries
        while (logs.children.length > 100) {
            logs.removeChild(logs.firstChild);
        }
    }
}

// Global functions for onclick handlers
window.sipManager = new SipClientManager();

window.closeEditModal = () => {
    sipManager.closeEditModal();
};

// Close modal when clicking outside
window.onclick = (event) => {
    const modal = document.getElementById('editModal');
    if (event.target === modal) {
        modal.style.display = 'none';
    }
};

// Initialize when page loads
document.addEventListener('DOMContentLoaded', () => {
    sipManager.log('🚀 AI Phone System initialized');
    sipManager.log('📡 Connecting to backend server...');
});
