-- Populate database with sample data for testing

-- Add sample SIP lines
INSERT OR IGNORE INTO sip_lines (extension, username, password, server_ip, server_port, display_name, enabled) VALUES 
('1001', 'user1001', 'pass123', '192.168.10.5', 5060, 'AI Assistant 1001', 1),
('1002', 'user1002', 'pass456', '192.168.10.5', 5060, 'AI Assistant 1002', 0),
('2001', 'reception', 'recpass', '192.168.10.10', 5060, 'Reception Line', 1),
('3001', 'support', 'support123', '192.168.10.15', 5060, 'Support Line', 0);

-- Add sample callers
INSERT OR IGNORE INTO callers (phone_number, created_at, last_call) VALUES 
('14', '2025-08-28 10:00:00', '2025-08-28 15:30:00'),
('15', '2025-08-28 11:15:00', '2025-08-28 14:20:00'),
('+49123456789', '2025-08-28 09:30:00', '2025-08-28 16:45:00'),
('+1234567890', '2025-08-28 08:45:00', '2025-08-28 13:10:00');

-- Add sample call sessions
INSERT OR IGNORE INTO call_sessions (session_id, caller_id, phone_number, line_id, user_id, start_time, whisper_data, llama_response, piper_audio_path) VALUES 
('session-001', 1, '14', 1, 'user-001', '2025-08-28 15:30:00', 'Hello, I need help with my account', 'Hello! I''d be happy to help you with your account. What specific issue are you experiencing?', '/audio/response-001.wav'),
('session-002', 2, '15', 1, 'user-002', '2025-08-28 14:20:00', 'Can you tell me about your services?', 'Certainly! We offer a range of AI-powered phone services including automated customer support, call routing, and voice transcription.', '/audio/response-002.wav'),
('session-003', 3, '+49123456789', 3, 'user-003', '2025-08-28 16:45:00', 'I want to cancel my subscription', 'I understand you''d like to cancel your subscription. Let me help you with that process. Can you please provide your account details?', '/audio/response-003.wav'),
('session-004', 1, '14', 1, 'user-001', '2025-08-28 12:15:00', 'Thank you for your help earlier', 'You''re very welcome! I''m glad I could assist you. Is there anything else you need help with today?', '/audio/response-004.wav');

-- Update system configuration
UPDATE system_config SET value = '4' WHERE key = 'system_speed';

-- Display current data
SELECT 'SIP Lines:' as info;
SELECT line_id, extension, server_ip, display_name, enabled, status FROM sip_lines;

SELECT 'Callers:' as info;
SELECT id, phone_number, last_call FROM callers;

SELECT 'Sessions:' as info;
SELECT session_id, phone_number, line_id, start_time, substr(whisper_data, 1, 30) || '...' as whisper_preview FROM call_sessions;

SELECT 'System Config:' as info;
SELECT key, value FROM system_config;
