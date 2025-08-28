<?php
// Whisper Talk LLaMA Dashboard
session_start();

// Configuration
$config = [
    'models_dir' => '../../../models',
    'whisper_models' => ['ggml-base.en.bin', 'ggml-small.en.bin', 'ggml-medium.en.bin', 'ggml-large.bin'],
    'llama_models' => ['ggml-llama-7B.bin', 'ggml-llama-13B.bin', 'ggml-llama-30B.bin'],
    'tts_voices' => ['default', 'female', 'male', 'child'],
    'api_base' => 'http://localhost:8081/api'
];

// Helper functions
function getSystemStatus() {
    return [
        'whisper' => ['status' => 'online', 'model' => 'ggml-base.en.bin', 'performance' => 'Good'],
        'llama' => ['status' => 'online', 'model' => 'ggml-llama-7B.bin', 'memory' => '4.2GB'],
        'piper' => ['status' => 'online', 'voice' => 'default', 'quality' => 'High'],
        'sip' => ['status' => 'online', 'clients' => 2, 'calls' => 0]
    ];
}

function scanAvailableModels($dir) {
    $models = [];
    if (is_dir($dir)) {
        $files = scandir($dir);
        foreach ($files as $file) {
            if (pathinfo($file, PATHINFO_EXTENSION) === 'bin') {
                $models[] = [
                    'name' => $file,
                    'size' => filesize($dir . '/' . $file),
                    'type' => strpos($file, 'whisper') !== false ? 'whisper' : 'llama',
                    'modified' => filemtime($dir . '/' . $file)
                ];
            }
        }
    }
    return $models;
}

function formatBytes($size) {
    $units = ['B', 'KB', 'MB', 'GB'];
    for ($i = 0; $size > 1024 && $i < count($units) - 1; $i++) {
        $size /= 1024;
    }
    return round($size, 2) . ' ' . $units[$i];
}

// Handle form submissions
if ($_POST) {
    if (isset($_POST['action'])) {
        switch ($_POST['action']) {
            case 'change_model':
                // Handle model change
                $_SESSION['message'] = "Model changed to: " . $_POST['model'];
                break;
            case 'upload_model':
                // Handle model upload
                if (isset($_FILES['model_file'])) {
                    $target = $config['models_dir'] . '/' . basename($_FILES['model_file']['name']);
                    if (move_uploaded_file($_FILES['model_file']['tmp_name'], $target)) {
                        $_SESSION['message'] = "Model uploaded successfully!";
                    } else {
                        $_SESSION['error'] = "Failed to upload model.";
                    }
                }
                break;
            case 'compile_coreml':
                // Handle CoreML compilation
                $_SESSION['message'] = "CoreML compilation started for: " . $_POST['model'];
                break;
        }
        header('Location: ' . $_SERVER['PHP_SELF']);
        exit;
    }
}

$current_tab = $_GET['tab'] ?? 'dashboard';
$system_status = getSystemStatus();
$available_models = scanAvailableModels($config['models_dir']);
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>🐱 Whisper Talk LLaMA - AI Phone System</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.10.0/font/bootstrap-icons.css" rel="stylesheet">
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>🐱</text></svg>">
    <style>
        body { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }
        .card { backdrop-filter: blur(10px); background: rgba(255, 255, 255, 0.95); border: none; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1); }
        .navbar { backdrop-filter: blur(10px); background: rgba(255, 255, 255, 0.95) !important; }
        .status-online { color: #28a745; }
        .status-offline { color: #dc3545; }
        .status-warning { color: #ffc107; }
        .hello-kitty { font-size: 2rem; filter: drop-shadow(2px 2px 4px rgba(0, 0, 0, 0.1)); }
        .metric-bar { height: 8px; background: #e9ecef; border-radius: 4px; overflow: hidden; }
        .metric-fill { height: 100%; background: linear-gradient(90deg, #28a745, #20c997); transition: width 0.5s ease; }
    </style>
</head>
<body>
    <!-- Navigation -->
    <nav class="navbar navbar-expand-lg navbar-light mb-4">
        <div class="container">
            <a class="navbar-brand" href="#">
                <span class="hello-kitty">🐱</span>
                <strong>Whisper Talk LLaMA</strong>
                <small class="text-muted">AI Phone System</small>
            </a>
            <div class="navbar-nav ms-auto">
                <span class="nav-link">
                    <i class="bi bi-circle-fill status-online"></i>
                    System Online
                </span>
            </div>
        </div>
    </nav>

    <div class="container">
        <!-- Messages -->
        <?php if (isset($_SESSION['message'])): ?>
            <div class="alert alert-success alert-dismissible fade show">
                <?= htmlspecialchars($_SESSION['message']) ?>
                <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
            </div>
            <?php unset($_SESSION['message']); ?>
        <?php endif; ?>

        <?php if (isset($_SESSION['error'])): ?>
            <div class="alert alert-danger alert-dismissible fade show">
                <?= htmlspecialchars($_SESSION['error']) ?>
                <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
            </div>
            <?php unset($_SESSION['error']); ?>
        <?php endif; ?>

        <!-- Tab Navigation -->
        <ul class="nav nav-pills mb-4 justify-content-center">
            <li class="nav-item">
                <a class="nav-link <?= $current_tab === 'dashboard' ? 'active' : '' ?>" href="?tab=dashboard">
                    <i class="bi bi-speedometer2"></i> Dashboard
                </a>
            </li>
            <li class="nav-item">
                <a class="nav-link <?= $current_tab === 'sip' ? 'active' : '' ?>" href="?tab=sip">
                    <i class="bi bi-telephone"></i> SIP Lines
                </a>
            </li>
            <li class="nav-item">
                <a class="nav-link <?= $current_tab === 'models' ? 'active' : '' ?>" href="?tab=models">
                    <i class="bi bi-cpu"></i> Models
                </a>
            </li>
            <li class="nav-item">
                <a class="nav-link <?= $current_tab === 'settings' ? 'active' : '' ?>" href="?tab=settings">
                    <i class="bi bi-gear"></i> Settings
                </a>
            </li>
        </ul>

        <!-- Dashboard Tab -->
        <?php if ($current_tab === 'dashboard'): ?>
            <div class="row">
                <!-- System Status -->
                <div class="col-lg-8 mb-4">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-activity"></i> System Status</h5>
                        </div>
                        <div class="card-body">
                            <div class="row">
                                <?php foreach ($system_status as $component => $status): ?>
                                    <div class="col-md-6 mb-3">
                                        <div class="d-flex align-items-center p-3 bg-light rounded">
                                            <div class="me-3">
                                                <?php
                                                $icons = ['whisper' => 'mic', 'llama' => 'brain', 'piper' => 'volume-up', 'sip' => 'telephone'];
                                                echo '<i class="bi bi-' . $icons[$component] . ' fs-2"></i>';
                                                ?>
                                            </div>
                                            <div class="flex-grow-1">
                                                <h6 class="mb-1"><?= ucfirst($component) ?></h6>
                                                <span class="status-<?= $status['status'] ?>">
                                                    <i class="bi bi-circle-fill"></i> <?= ucfirst($status['status']) ?>
                                                </span>
                                                <div class="small text-muted mt-1">
                                                    <?php if ($component === 'whisper'): ?>
                                                        Model: <?= $status['model'] ?><br>
                                                        Performance: <?= $status['performance'] ?>
                                                    <?php elseif ($component === 'llama'): ?>
                                                        Model: <?= $status['model'] ?><br>
                                                        Memory: <?= $status['memory'] ?>
                                                    <?php elseif ($component === 'piper'): ?>
                                                        Voice: <?= $status['voice'] ?><br>
                                                        Quality: <?= $status['quality'] ?>
                                                    <?php elseif ($component === 'sip'): ?>
                                                        Clients: <?= $status['clients'] ?><br>
                                                        Active Calls: <?= $status['calls'] ?>
                                                    <?php endif; ?>
                                                </div>
                                            </div>
                                        </div>
                                    </div>
                                <?php endforeach; ?>
                            </div>
                        </div>
                    </div>
                </div>

                <!-- Performance Metrics -->
                <div class="col-lg-4 mb-4">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-graph-up"></i> Performance</h5>
                        </div>
                        <div class="card-body">
                            <div class="mb-3">
                                <div class="d-flex justify-content-between mb-1">
                                    <span>CPU Usage</span>
                                    <span>45%</span>
                                </div>
                                <div class="metric-bar">
                                    <div class="metric-fill" style="width: 45%"></div>
                                </div>
                            </div>
                            <div class="mb-3">
                                <div class="d-flex justify-content-between mb-1">
                                    <span>Memory</span>
                                    <span>62%</span>
                                </div>
                                <div class="metric-bar">
                                    <div class="metric-fill" style="width: 62%"></div>
                                </div>
                            </div>
                            <div class="mb-3">
                                <div class="d-flex justify-content-between mb-1">
                                    <span>GPU Usage</span>
                                    <span>28%</span>
                                </div>
                                <div class="metric-bar">
                                    <div class="metric-fill" style="width: 28%"></div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Active Calls & Recent Activity -->
            <div class="row">
                <div class="col-lg-6 mb-4">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-telephone-inbound"></i> Active Calls</h5>
                        </div>
                        <div class="card-body">
                            <div class="text-center text-muted py-4">
                                <i class="bi bi-telephone-x fs-1"></i>
                                <p class="mt-2">No active calls</p>
                            </div>
                        </div>
                    </div>
                </div>

                <div class="col-lg-6 mb-4">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-clock-history"></i> Recent Activity</h5>
                        </div>
                        <div class="card-body">
                            <div class="list-group list-group-flush">
                                <div class="list-group-item border-0 px-0">
                                    <div class="d-flex justify-content-between">
                                        <span>System started</span>
                                        <small class="text-muted">2 min ago</small>
                                    </div>
                                </div>
                                <div class="list-group-item border-0 px-0">
                                    <div class="d-flex justify-content-between">
                                        <span>Models loaded</span>
                                        <small class="text-muted">1 min ago</small>
                                    </div>
                                </div>
                                <div class="list-group-item border-0 px-0">
                                    <div class="d-flex justify-content-between">
                                        <span>SIP server ready</span>
                                        <small class="text-muted">30 sec ago</small>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        <?php endif; ?>

        <!-- Models Tab -->
        <?php if ($current_tab === 'models'): ?>
            <div class="row">
                <!-- Current Models -->
                <div class="col-lg-6 mb-4">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-cpu-fill"></i> Currently Loaded Models</h5>
                        </div>
                        <div class="card-body">
                            <div class="list-group list-group-flush">
                                <div class="list-group-item d-flex justify-content-between align-items-center px-0">
                                    <div>
                                        <strong>Whisper</strong><br>
                                        <small class="text-muted font-monospace">ggml-base.en.bin</small>
                                    </div>
                                    <button class="btn btn-outline-primary btn-sm" data-bs-toggle="modal" data-bs-target="#changeModelModal" data-type="whisper">
                                        Change
                                    </button>
                                </div>
                                <div class="list-group-item d-flex justify-content-between align-items-center px-0">
                                    <div>
                                        <strong>LLaMA</strong><br>
                                        <small class="text-muted font-monospace">ggml-llama-7B.bin</small>
                                    </div>
                                    <button class="btn btn-outline-primary btn-sm" data-bs-toggle="modal" data-bs-target="#changeModelModal" data-type="llama">
                                        Change
                                    </button>
                                </div>
                                <div class="list-group-item d-flex justify-content-between align-items-center px-0">
                                    <div>
                                        <strong>TTS Voice</strong><br>
                                        <small class="text-muted">Default</small>
                                    </div>
                                    <button class="btn btn-outline-primary btn-sm" data-bs-toggle="modal" data-bs-target="#changeModelModal" data-type="tts">
                                        Change
                                    </button>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>

                <!-- Model Actions -->
                <div class="col-lg-6 mb-4">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-tools"></i> Model Management</h5>
                        </div>
                        <div class="card-body">
                            <div class="d-grid gap-2">
                                <button class="btn btn-primary" data-bs-toggle="modal" data-bs-target="#uploadModelModal">
                                    <i class="bi bi-upload"></i> Upload Model
                                </button>
                                <button class="btn btn-secondary" data-bs-toggle="modal" data-bs-target="#downloadModelModal">
                                    <i class="bi bi-download"></i> Download from OpenAI
                                </button>
                                <button class="btn btn-warning" data-bs-toggle="modal" data-bs-target="#compileCoreMLModal">
                                    <i class="bi bi-gear-fill"></i> Compile CoreML
                                </button>
                                <a href="?tab=models&action=scan" class="btn btn-outline-secondary">
                                    <i class="bi bi-arrow-clockwise"></i> Scan Local Models
                                </a>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Available Models -->
            <div class="card">
                <div class="card-header">
                    <h5 class="card-title mb-0"><i class="bi bi-collection"></i> Available Models</h5>
                </div>
                <div class="card-body">
                    <?php if (empty($available_models)): ?>
                        <div class="text-center text-muted py-4">
                            <i class="bi bi-folder2-open fs-1"></i>
                            <p class="mt-2">No models found. Upload or download models to get started.</p>
                        </div>
                    <?php else: ?>
                        <div class="table-responsive">
                            <table class="table table-hover">
                                <thead>
                                    <tr>
                                        <th>Model Name</th>
                                        <th>Type</th>
                                        <th>Size</th>
                                        <th>Modified</th>
                                        <th>Actions</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    <?php foreach ($available_models as $model): ?>
                                        <tr>
                                            <td class="font-monospace"><?= htmlspecialchars($model['name']) ?></td>
                                            <td>
                                                <span class="badge bg-<?= $model['type'] === 'whisper' ? 'primary' : 'success' ?>">
                                                    <?= ucfirst($model['type']) ?>
                                                </span>
                                            </td>
                                            <td><?= formatBytes($model['size']) ?></td>
                                            <td><?= date('Y-m-d H:i', $model['modified']) ?></td>
                                            <td>
                                                <div class="btn-group btn-group-sm">
                                                    <button class="btn btn-outline-primary" onclick="loadModel('<?= $model['name'] ?>')">
                                                        Load
                                                    </button>
                                                    <button class="btn btn-outline-warning" onclick="compileCoreML('<?= $model['name'] ?>')">
                                                        CoreML
                                                    </button>
                                                </div>
                                            </td>
                                        </tr>
                                    <?php endforeach; ?>
                                </tbody>
                            </table>
                        </div>
                    <?php endif; ?>
                </div>
            </div>
        <?php endif; ?>

        <!-- SIP Lines Tab -->
        <?php if ($current_tab === 'sip'): ?>
            <div class="row">
                <div class="col-12 mb-4">
                    <div class="card">
                        <div class="card-header d-flex justify-content-between align-items-center">
                            <h5 class="card-title mb-0"><i class="bi bi-telephone-fill"></i> SIP Lines</h5>
                            <button class="btn btn-primary" data-bs-toggle="modal" data-bs-target="#addSipLineModal">
                                <i class="bi bi-plus-circle"></i> Add SIP Line
                            </button>
                        </div>
                        <div class="card-body">
                            <div class="row">
                                <!-- Example SIP Line -->
                                <div class="col-lg-6 mb-3">
                                    <div class="card border">
                                        <div class="card-body">
                                            <div class="d-flex justify-content-between align-items-start mb-2">
                                                <h6 class="card-title">SIP Line 1</h6>
                                                <span class="badge bg-success">Online</span>
                                            </div>
                                            <p class="card-text">
                                                <strong>Extension:</strong> 1001<br>
                                                <strong>Server:</strong> sip.example.com:5060<br>
                                                <strong>Status:</strong> Registered<br>
                                                <strong>Calls Today:</strong> 5
                                            </p>
                                            <div class="btn-group btn-group-sm">
                                                <button class="btn btn-outline-primary">Edit</button>
                                                <button class="btn btn-outline-danger">Remove</button>
                                                <button class="btn btn-outline-info">Stats</button>
                                            </div>
                                        </div>
                                    </div>
                                </div>

                                <div class="col-lg-6 mb-3">
                                    <div class="card border">
                                        <div class="card-body">
                                            <div class="d-flex justify-content-between align-items-start mb-2">
                                                <h6 class="card-title">SIP Line 2</h6>
                                                <span class="badge bg-warning">Connecting</span>
                                            </div>
                                            <p class="card-text">
                                                <strong>Extension:</strong> 1002<br>
                                                <strong>Server:</strong> sip.example.com:5060<br>
                                                <strong>Status:</strong> Registering<br>
                                                <strong>Calls Today:</strong> 0
                                            </p>
                                            <div class="btn-group btn-group-sm">
                                                <button class="btn btn-outline-primary">Edit</button>
                                                <button class="btn btn-outline-danger">Remove</button>
                                                <button class="btn btn-outline-info">Stats</button>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        <?php endif; ?>

        <!-- Settings Tab -->
        <?php if ($current_tab === 'settings'): ?>
            <div class="row">
                <div class="col-lg-8 mx-auto">
                    <div class="card">
                        <div class="card-header">
                            <h5 class="card-title mb-0"><i class="bi bi-gear-fill"></i> System Settings</h5>
                        </div>
                        <div class="card-body">
                            <form method="post">
                                <input type="hidden" name="action" value="save_settings">

                                <div class="mb-3">
                                    <label class="form-label">HTTP Server Port</label>
                                    <input type="number" class="form-control" name="http_port" value="8081">
                                </div>

                                <div class="mb-3">
                                    <label class="form-label">Default Person Name</label>
                                    <input type="text" class="form-control" name="person_name" value="Georgi">
                                </div>

                                <div class="mb-3">
                                    <label class="form-label">Bot Name</label>
                                    <input type="text" class="form-control" name="bot_name" value="LLaMA">
                                </div>

                                <div class="mb-3">
                                    <label class="form-label">Language</label>
                                    <select class="form-select" name="language">
                                        <option value="en" selected>English</option>
                                        <option value="de">German</option>
                                        <option value="fr">French</option>
                                        <option value="es">Spanish</option>
                                    </select>
                                </div>

                                <div class="mb-3">
                                    <div class="form-check">
                                        <input class="form-check-input" type="checkbox" name="use_gpu" checked>
                                        <label class="form-check-label">Use GPU Acceleration</label>
                                    </div>
                                </div>

                                <div class="mb-3">
                                    <div class="form-check">
                                        <input class="form-check-input" type="checkbox" name="flash_attn">
                                        <label class="form-check-label">Enable Flash Attention</label>
                                    </div>
                                </div>

                                <button type="submit" class="btn btn-primary">Save Settings</button>
                            </form>
                        </div>
                    </div>
                </div>
            </div>
        <?php endif; ?>
    </div>

    <!-- Modals -->

    <!-- Change Model Modal -->
    <div class="modal fade" id="changeModelModal" tabindex="-1">
        <div class="modal-dialog">
            <div class="modal-content">
                <div class="modal-header">
                    <h5 class="modal-title">Change Model</h5>
                    <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                </div>
                <form method="post">
                    <div class="modal-body">
                        <input type="hidden" name="action" value="change_model">
                        <input type="hidden" name="model_type" id="modelType">

                        <div class="mb-3">
                            <label class="form-label">Select Model</label>
                            <select class="form-select" name="model" required>
                                <option value="">Choose a model...</option>
                                <?php foreach ($available_models as $model): ?>
                                    <option value="<?= htmlspecialchars($model['name']) ?>">
                                        <?= htmlspecialchars($model['name']) ?> (<?= formatBytes($model['size']) ?>)
                                    </option>
                                <?php endforeach; ?>
                            </select>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                        <button type="submit" class="btn btn-primary">Change Model</button>
                    </div>
                </form>
            </div>
        </div>
    </div>

    <!-- Upload Model Modal -->
    <div class="modal fade" id="uploadModelModal" tabindex="-1">
        <div class="modal-dialog">
            <div class="modal-content">
                <div class="modal-header">
                    <h5 class="modal-title">Upload Model</h5>
                    <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                </div>
                <form method="post" enctype="multipart/form-data">
                    <div class="modal-body">
                        <input type="hidden" name="action" value="upload_model">

                        <div class="mb-3">
                            <label class="form-label">Select Model File</label>
                            <input type="file" class="form-control" name="model_file" accept=".bin,.gguf" required>
                            <div class="form-text">Supported formats: .bin, .gguf</div>
                        </div>

                        <div class="mb-3">
                            <label class="form-label">Model Type</label>
                            <select class="form-select" name="model_type" required>
                                <option value="">Select type...</option>
                                <option value="whisper">Whisper (Speech-to-Text)</option>
                                <option value="llama">LLaMA (Language Model)</option>
                            </select>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                        <button type="submit" class="btn btn-primary">Upload Model</button>
                    </div>
                </form>
            </div>
        </div>
    </div>

    <!-- Download Model Modal -->
    <div class="modal fade" id="downloadModelModal" tabindex="-1">
        <div class="modal-dialog">
            <div class="modal-content">
                <div class="modal-header">
                    <h5 class="modal-title">Download Model from OpenAI</h5>
                    <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                </div>
                <form method="post">
                    <div class="modal-body">
                        <input type="hidden" name="action" value="download_model">

                        <div class="mb-3">
                            <label class="form-label">Model Repository</label>
                            <select class="form-select" name="repo" required>
                                <option value="">Select repository...</option>
                                <option value="openai/whisper-base">Whisper Base</option>
                                <option value="openai/whisper-small">Whisper Small</option>
                                <option value="openai/whisper-medium">Whisper Medium</option>
                                <option value="openai/whisper-large">Whisper Large</option>
                            </select>
                        </div>

                        <div class="alert alert-info">
                            <i class="bi bi-info-circle"></i>
                            Models will be downloaded and automatically converted to the appropriate format.
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                        <button type="submit" class="btn btn-primary">Download Model</button>
                    </div>
                </form>
            </div>
        </div>
    </div>

    <!-- Compile CoreML Modal -->
    <div class="modal fade" id="compileCoreMLModal" tabindex="-1">
        <div class="modal-dialog">
            <div class="modal-content">
                <div class="modal-header">
                    <h5 class="modal-title">Compile CoreML Model</h5>
                    <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                </div>
                <form method="post">
                    <div class="modal-body">
                        <input type="hidden" name="action" value="compile_coreml">

                        <div class="mb-3">
                            <label class="form-label">Source Model</label>
                            <select class="form-select" name="model" required>
                                <option value="">Select model to compile...</option>
                                <?php foreach ($available_models as $model): ?>
                                    <option value="<?= htmlspecialchars($model['name']) ?>">
                                        <?= htmlspecialchars($model['name']) ?>
                                    </option>
                                <?php endforeach; ?>
                            </select>
                        </div>

                        <div class="alert alert-warning">
                            <i class="bi bi-exclamation-triangle"></i>
                            <strong>Apple Silicon Optimization</strong><br>
                            This will compile the model for optimal performance on Apple Silicon using CoreML.
                            The process may take several minutes.
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                        <button type="submit" class="btn btn-warning">Compile CoreML</button>
                    </div>
                </form>
            </div>
        </div>
    </div>

    <!-- Add SIP Line Modal -->
    <div class="modal fade" id="addSipLineModal" tabindex="-1">
        <div class="modal-dialog">
            <div class="modal-content">
                <div class="modal-header">
                    <h5 class="modal-title">Add SIP Line</h5>
                    <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                </div>
                <form method="post">
                    <div class="modal-body">
                        <input type="hidden" name="action" value="add_sip_line">

                        <div class="row">
                            <div class="col-md-6 mb-3">
                                <label class="form-label">Extension</label>
                                <input type="text" class="form-control" name="extension" required>
                            </div>
                            <div class="col-md-6 mb-3">
                                <label class="form-label">Display Name</label>
                                <input type="text" class="form-control" name="display_name">
                            </div>
                        </div>

                        <div class="row">
                            <div class="col-md-8 mb-3">
                                <label class="form-label">SIP Server</label>
                                <input type="text" class="form-control" name="server_ip" placeholder="sip.example.com" required>
                            </div>
                            <div class="col-md-4 mb-3">
                                <label class="form-label">Port</label>
                                <input type="number" class="form-control" name="server_port" value="5060" required>
                            </div>
                        </div>

                        <div class="row">
                            <div class="col-md-6 mb-3">
                                <label class="form-label">Username</label>
                                <input type="text" class="form-control" name="username" required>
                            </div>
                            <div class="col-md-6 mb-3">
                                <label class="form-label">Password</label>
                                <input type="password" class="form-control" name="password" required>
                            </div>
                        </div>

                        <div class="mb-3">
                            <div class="form-check">
                                <input class="form-check-input" type="checkbox" name="auto_answer" checked>
                                <label class="form-check-label">Auto Answer Calls</label>
                            </div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                        <button type="submit" class="btn btn-primary">Add SIP Line</button>
                    </div>
                </form>
            </div>
        </div>
    </div>

    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
    <script>
        // Minimal JavaScript for modal data passing
        document.addEventListener('DOMContentLoaded', function() {
            var changeModelModal = document.getElementById('changeModelModal');
            changeModelModal.addEventListener('show.bs.modal', function (event) {
                var button = event.relatedTarget;
                var type = button.getAttribute('data-type');
                var modalTypeInput = changeModelModal.querySelector('#modelType');
                modalTypeInput.value = type;
            });
        });

        function loadModel(modelName) {
            if (confirm('Load model: ' + modelName + '?')) {
                window.location.href = '?tab=models&action=load&model=' + encodeURIComponent(modelName);
            }
        }

        function compileCoreML(modelName) {
            if (confirm('Compile CoreML for: ' + modelName + '? This may take several minutes.')) {
                window.location.href = '?tab=models&action=compile_coreml&model=' + encodeURIComponent(modelName);
            }
        }
    </script>
</body>
</html>
